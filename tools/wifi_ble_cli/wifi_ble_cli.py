#!/usr/bin/env python3
"""
ESP WiFi Config BLE CLI Client (protocomm)

A command-line tool to provision and inspect ESP32 devices running the
esp_wifi_config library's Network Provisioning (BLE scheme) backend.

This talks Espressif's standard protocomm protocol (protobuf over GATT)
plus the optional `esp-wifi-config-*` JSON endpoints the library exposes
when `expose_library_endpoints = true`.

A vendored copy of esp_prov's protocol modules lives in ./esp_prov/.
"""

import asyncio
import json
import os
import sys
from typing import Optional

import click

# Vendored esp_prov modules use bare imports (e.g. `import prov`). Put the
# vendored package directory on sys.path so they resolve.
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, 'esp_prov'))

import prov                  # noqa: E402
import security              # noqa: E402
import transport             # noqa: E402
from bleak import BleakScanner  # noqa: E402

# Service UUID and 16-bit endpoint suffixes match Espressif's default
# wifi_provisioning scheme_ble service. The real characteristic UUIDs are
# discovered at runtime via the 0x2901 user-description descriptors, so
# these only act as a fallback when discovery fails.
SERVICE_UUID = '021a9004-0382-4aea-bff4-6b3f1c5adfb4'
NU_LOOKUP_FALLBACK = {
    'prov-session': 'ff51',
    'prov-config': 'ff52',
    'proto-ver': 'ff53',
}

# Library extension endpoint names (esp_wifi_config_prov_ble.c).
LIB_EP_VERSION       = 'esp-wifi-config-version'
LIB_EP_CAPABILITIES  = 'esp-wifi-config-capabilities'
LIB_EP_VARS          = 'esp-wifi-config-vars'
LIB_EP_NETWORK_POLICY = 'esp-wifi-config-network-policy'


# ---------------------------------------------------------------------------
# Provisioning session helper
# ---------------------------------------------------------------------------

class ProvSession:
    """One BLE connection + security handshake. Use via `async with`."""

    def __init__(self, devname: str, sec_ver: int, pop: str,
                 sec2_user: str, sec2_pwd: str, verbose: bool):
        self.devname = devname
        self.sec_ver = sec_ver
        self.pop = pop
        self.sec2_user = sec2_user
        self.sec2_pwd = sec2_pwd
        self.verbose = verbose
        self.sec = None
        self.tp = None

    async def __aenter__(self):
        self.tp = transport.Transport_BLE(
            service_uuid=SERVICE_UUID,
            nu_lookup=dict(NU_LOOKUP_FALLBACK),
        )
        await self.tp.connect(devname=self.devname)

        # Read proto-ver (plaintext) BEFORE building the security context, so
        # we can pick up the device's security parameters (sec_patch_ver, the
        # no_pop capability). Espressif firmware ignores the request body.
        prov_info = {}
        try:
            info = await self.proto_ver()
            if isinstance(info, dict):
                prov_info = info.get('prov', {}) or {}
        except Exception:
            pass
        caps = prov_info.get('cap', []) or []
        pop = '' if 'no_pop' in caps else self.pop

        if self.sec_ver == 0:
            self.sec = security.Security0(verbose=self.verbose)
        elif self.sec_ver == 1:
            self.sec = security.Security1(pop, verbose=self.verbose)
        elif self.sec_ver == 2:
            if not self.sec2_user or not self.sec2_pwd:
                raise click.ClickException(
                    'sec_ver=2 requires --sec2-user and --sec2-pwd')
            # Trust the device's advertised sec_patch_ver. Default a MISSING
            # value to 1 (the only interoperable mode) — never to 0, which
            # would select the dead/incompatible reused-nonce path.
            sec_patch_ver = prov_info.get('sec_patch_ver', 1)
            self.sec = security.Security2(sec_patch_ver, self.sec2_user,
                                          self.sec2_pwd, verbose=self.verbose)
        else:
            raise click.ClickException(f'invalid sec_ver: {self.sec_ver}')

        # Run the security FSM until it stops emitting requests. A wrong
        # PoP/password makes the device reject the proof and drop the GATT
        # link mid-handshake (surfacing as a BleakError or a verify
        # RuntimeError); turn that into a clean, actionable message.
        response = None
        try:
            while True:
                req = self.sec.security_session(response)
                if req is None:
                    break
                response = await self.tp.send_data('prov-session', req)
        except click.ClickException:
            raise
        except Exception as e:
            cred = '--pop' if self.sec_ver == 1 else (
                '--sec2-user/--sec2-pwd' if self.sec_ver == 2 else 'credentials')
            raise click.ClickException(
                f'security handshake failed (sec_ver={self.sec_ver}). '
                f'Wrong {cred}, or the device is busy with another client. [{type(e).__name__}: {e}]')
        return self

    async def __aexit__(self, exc_type, exc, tb):
        if self.tp is not None:
            try:
                await self.tp.disconnect()
            except Exception:
                pass

    # ---- standard provisioning endpoints ----

    async def proto_ver(self) -> dict:
        """Read the proto-ver endpoint. Returns parsed JSON or {}."""
        # The firmware ignores the request body; "ESP" is the conventional probe.
        resp = await self.tp.send_data('proto-ver', 'ESP')
        try:
            return json.loads(resp)
        except ValueError:
            return {}

    async def scan_wifi(self) -> list[dict]:
        """Run prov-scan and return a list of AP dicts."""
        msg = prov.scan_start_request(self.sec, blocking=True, group_channels=0)
        resp = await self.tp.send_data('prov-scan', msg)
        prov.scan_start_response(self.sec, resp)

        # With blocking=True the scan is already done by the time ScanStart
        # returns, but poll ScanStatus until finished to be robust. Re-encrypt
        # the request each iteration — reusing one ciphertext would desync the
        # Sec1 CTR stream / Sec2 GCM counter.
        tries = 0
        while True:
            msg = prov.scan_status_request(self.sec)
            resp = await self.tp.send_data('prov-scan', msg)
            st = prov.scan_status_response(self.sec, resp)
            tries += 1
            if st['finished'] or tries >= 20:
                break
            await asyncio.sleep(0.2)
        count = st['count']

        APs = []
        readlen = 4
        index = 0
        remaining = count
        while remaining:
            n = readlen if remaining > readlen else remaining
            msg = prov.scan_result_request(self.sec, index, n)
            resp = await self.tp.send_data('prov-scan', msg)
            APs += prov.scan_result_response(self.sec, resp)
            remaining -= n
            index += n
        return APs

    async def set_config(self, ssid: str, passphrase: str):
        # Firmware rejects SSID >= 32 bytes / passphrase >= 64 bytes (UTF-8)
        # with InvalidArgument. Validate locally for a clear error.
        if len(ssid.encode('utf-8')) >= 32:
            raise click.ClickException('SSID must be < 32 bytes (UTF-8)')
        if passphrase and len(passphrase.encode('utf-8')) >= 64:
            raise click.ClickException('passphrase must be < 64 bytes (UTF-8)')
        msg = prov.config_set_config_request(self.sec, ssid, passphrase)
        resp = await self.tp.send_data('prov-config', msg)
        if prov.config_set_config_response(self.sec, resp) != 0:
            raise click.ClickException('device rejected set_config')

    async def apply_config(self):
        msg = prov.config_apply_config_request(self.sec)
        resp = await self.tp.send_data('prov-config', msg)
        if prov.config_apply_config_response(self.sec, resp) != 0:
            raise click.ClickException('device rejected apply_config')

    async def wifi_status(self) -> str:
        """Returns a status string: 'connected', 'connecting',
        'disconnected', 'failed', or 'unknown'."""
        msg = prov.config_get_status_request(self.sec)
        resp = await self.tp.send_data('prov-config', msg)
        return prov.config_get_status_response(self.sec, resp)

    async def ctrl_reset(self):
        msg = prov.ctrl_reset_request(self.sec)
        resp = await self.tp.send_data('prov-ctrl', msg)
        if prov.ctrl_reset_response(self.sec, resp) != 0:
            raise click.ClickException('device rejected ctrl_reset')

    async def ctrl_reprov(self):
        msg = prov.ctrl_reprov_request(self.sec)
        resp = await self.tp.send_data('prov-ctrl', msg)
        if prov.ctrl_reprov_response(self.sec, resp) != 0:
            raise click.ClickException('device rejected ctrl_reprov')

    # ---- library-extension JSON endpoints ----

    async def lib_json(self, ep_name: str, payload: Optional[dict] = None) -> dict:
        """Call a library-extension endpoint with optional JSON payload.
        Returns the parsed JSON response.
        """
        # Send a minimal non-empty body even for payload-less reads: an empty
        # encrypted request does not round-trip through protocomm-over-BLE
        # (the device returns an empty value). The read endpoints ignore the
        # request content, so "{}" is a harmless, valid-JSON probe.
        body = json.dumps(payload) if payload is not None else '{}'
        # encrypt_data is a no-op for sec0 and full crypto for sec1/sec2;
        # custom_data_request handles both.
        msg = prov.custom_data_request(self.sec, body)
        try:
            resp = await self.tp.send_data(ep_name, msg)
        except RuntimeError as e:
            raise click.ClickException(
                f"endpoint '{ep_name}' not available "
                f"(firmware likely built with expose_library_endpoints=false): {e}")
        from utils import str_to_bytes
        plain = self.sec.decrypt_data(str_to_bytes(resp))
        try:
            return json.loads(plain)
        except ValueError:
            raise click.ClickException(
                f"endpoint '{ep_name}' returned non-JSON: {plain!r}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _make_session(ctx) -> ProvSession:
    o = ctx.obj
    if not o.get('name'):
        raise click.ClickException(
            "device name required (use --name / -n; e.g. 'TiltBridge-XXXXXX')")
    return ProvSession(
        devname=o['name'],
        sec_ver=o['sec_ver'],
        pop=o['pop'],
        sec2_user=o['sec2_user'],
        sec2_pwd=o['sec2_pwd'],
        verbose=o['verbose'],
    )


@click.group(context_settings={'help_option_names': ['-h', '--help']})
@click.option('-n', '--name', help='BLE device name (e.g. "TiltBridge-E3F6B0")')
@click.option('--sec-ver', type=click.IntRange(0, 2), default=1, show_default=True,
              help='Protocomm security version (0=none, 1=X25519+PoP, 2=SRP6a)')
@click.option('--pop', default='', help='Proof of Possession (sec_ver=1)')
@click.option('--sec2-user', default='', help='SRP6a username (sec_ver=2)')
@click.option('--sec2-pwd', default='', help='SRP6a password (sec_ver=2)')
@click.option('-v', '--verbose', is_flag=True,
              help='Print protocol-level trace (encrypted bytes, decode steps)')
@click.pass_context
def cli(ctx, name, sec_ver, pop, sec2_user, sec2_pwd, verbose):
    """ESP WiFi Config BLE CLI Client.

    Talks Espressif's standard provisioning protocol (protocomm over BLE)
    to a device running the esp_wifi_config library. Some commands require
    the firmware to opt into the library's extension endpoints via
    `expose_library_endpoints = true`; those are noted in their help text.
    """
    ctx.ensure_object(dict)
    ctx.obj.update({
        'name': name, 'sec_ver': sec_ver, 'pop': pop,
        'sec2_user': sec2_user, 'sec2_pwd': sec2_pwd,
        'verbose': verbose,
    })


@cli.command()
@click.option('--timeout', type=float, default=8.0, show_default=True)
def devices(timeout):
    """Scan all nearby BLE devices and print names + addresses."""
    async def run():
        found = await BleakScanner.discover(timeout=timeout, return_adv=True)
        rows = []
        for addr, (dev, adv) in found.items():
            # On macOS the name may live in advertisement_data.local_name
            # rather than BLEDevice.name.
            name = dev.name or adv.local_name or ''
            rows.append((name, addr))
        rows.sort(key=lambda r: (r[0] == '', r[0].lower()))
        click.echo(f"{'Name':<32} Address")
        click.echo('-' * 70)
        for name, addr in rows:
            click.echo(f"{name:<32} {addr}")
    asyncio.run(run())


@cli.command()
@click.pass_context
def status(ctx):
    """Read proto-ver: prov protocol version + capability list."""
    async def run():
        async with _make_session(ctx) as sess:
            info = await sess.proto_ver()
            click.echo(json.dumps(info, indent=2) if info else '(empty)')
    asyncio.run(run())


@cli.command()
@click.pass_context
def scan(ctx):
    """Ask the device to scan for nearby Wi-Fi APs."""
    async def run():
        async with _make_session(ctx) as sess:
            APs = await sess.scan_wifi()
        if not APs:
            click.echo('No APs found.')
            return
        click.echo(f"\n{'SSID':<33} {'BSSID':<14} {'CH':>3} {'RSSI':>5}  AUTH")
        click.echo('-' * 75)
        for ap in APs:
            click.echo(f"{ap['ssid'][:32]:<33} {ap['bssid']:<14} "
                       f"{ap['channel']:>3} {ap['rssi']:>5}  {ap['auth']}")
    asyncio.run(run())


@cli.command()
@click.argument('ssid')
@click.argument('passphrase')
@click.option('--wait-timeout', type=float, default=45.0, show_default=True,
              help='How long to wait for the device to report Wi-Fi connected')
@click.pass_context
def provision(ctx, ssid, passphrase, wait_timeout):
    """Send Wi-Fi credentials and wait for the device to confirm connection."""
    import time
    async def run():
        async with _make_session(ctx) as sess:
            click.echo(f'Sending credentials for "{ssid}"...')
            await sess.set_config(ssid, passphrase)
            await sess.apply_config()
            click.echo('Applied. Waiting for connection...')

            # wifi_status() returns 'connected'/'connecting'/'disconnected'/
            # 'failed'/'unknown'. The upstream response handler also prints a
            # human-readable line per poll, so we don't echo extra state here.
            from bleak.exc import BleakError
            deadline = time.time() + wait_timeout
            while time.time() < deadline:
                try:
                    state = await sess.wifi_status()
                except BleakError as e:
                    # Firmware with stop_provisioning_on_connect=true tears
                    # down BLE the moment Wi-Fi comes up, which kills the
                    # link mid-poll. Most likely the device just connected
                    # successfully and we lost the radio.
                    click.echo(f'BLE link dropped during polling ({e}).')
                    click.echo('This usually means the device connected and '
                               'tore down provisioning. Verify out-of-band.')
                    return
                if state == 'connected':
                    click.echo('Provisioning successful.')
                    return
                if state == 'failed':
                    raise click.ClickException(
                        'device reported connection failure')
                await asyncio.sleep(1)
            raise click.ClickException(
                f'timeout after {wait_timeout}s waiting for connected')
    asyncio.run(run())


@cli.command('wifi-status')
@click.pass_context
def wifi_status(ctx):
    """Read the device's current Wi-Fi connection state."""
    async def run():
        async with _make_session(ctx) as sess:
            state = await sess.wifi_status()
            click.echo(f'state: {state}')
    asyncio.run(run())


@cli.command()
@click.confirmation_option(prompt='Clear stored Wi-Fi credentials on the device?')
@click.pass_context
def reset(ctx):
    """Tell the device to clear its stored Wi-Fi credentials (prov-ctrl reset)."""
    async def run():
        async with _make_session(ctx) as sess:
            await sess.ctrl_reset()
        click.echo('Reset sent.')
    asyncio.run(run())


@cli.command()
@click.pass_context
def reprov(ctx):
    """Tell the device to re-enter provisioning mode (prov-ctrl reprov)."""
    async def run():
        async with _make_session(ctx) as sess:
            await sess.ctrl_reprov()
        click.echo('Reprov sent.')
    asyncio.run(run())


# ---- library-extension endpoint commands ---------------------------------
# All of these require the firmware to have set expose_library_endpoints=true
# in wifi_cfg_prov_config_t. Otherwise the endpoint name won't resolve and
# the command will surface a clear error.

@cli.command('lib-version')
@click.pass_context
def lib_version(ctx):
    """[lib-ext] Read esp-wifi-config-version (library/IDF/firmware versions)."""
    async def run():
        async with _make_session(ctx) as sess:
            click.echo(json.dumps(await sess.lib_json(LIB_EP_VERSION), indent=2))
    asyncio.run(run())


@cli.command('lib-capabilities')
@click.pass_context
def lib_capabilities(ctx):
    """[lib-ext] Read esp-wifi-config-capabilities (enabled features)."""
    async def run():
        async with _make_session(ctx) as sess:
            click.echo(json.dumps(await sess.lib_json(LIB_EP_CAPABILITIES), indent=2))
    asyncio.run(run())


@cli.command('network-policy')
@click.pass_context
def network_policy(ctx):
    """[lib-ext] Read esp-wifi-config-network-policy (retry/reconnect config)."""
    async def run():
        async with _make_session(ctx) as sess:
            click.echo(json.dumps(await sess.lib_json(LIB_EP_NETWORK_POLICY), indent=2))
    asyncio.run(run())


@cli.command('list-vars')
@click.pass_context
def list_vars(ctx):
    """[lib-ext] List all custom variables on the device."""
    async def run():
        async with _make_session(ctx) as sess:
            data = await sess.lib_json(LIB_EP_VARS, {'op': 'list'})
        vars_ = data.get('vars', [])
        if not vars_:
            click.echo('(no variables)')
            return
        for v in vars_:
            click.echo(f"{v.get('k')}={v.get('v')}")
    asyncio.run(run())


@cli.command('get-var')
@click.argument('key')
@click.pass_context
def get_var(ctx, key):
    """[lib-ext] Get a custom variable's value."""
    async def run():
        async with _make_session(ctx) as sess:
            data = await sess.lib_json(LIB_EP_VARS, {'op': 'get', 'key': key})
        if 'error' in data:
            raise click.ClickException(data['error'])
        click.echo(f"{data.get('key')}={data.get('value')}")
    asyncio.run(run())


@cli.command('set-var')
@click.argument('key')
@click.argument('value')
@click.pass_context
def set_var(ctx, key, value):
    """[lib-ext] Set a custom variable."""
    async def run():
        async with _make_session(ctx) as sess:
            data = await sess.lib_json(LIB_EP_VARS,
                                       {'op': 'set', 'key': key, 'value': value})
        if 'error' in data:
            raise click.ClickException(data['error'])
        click.echo(f"Set {key}.")
    asyncio.run(run())


@cli.command('del-var')
@click.argument('key')
@click.pass_context
def del_var(ctx, key):
    """[lib-ext] Delete a custom variable."""
    async def run():
        async with _make_session(ctx) as sess:
            data = await sess.lib_json(LIB_EP_VARS, {'op': 'del', 'key': key})
        if data.get('error'):
            raise click.ClickException(data['error'])
        click.echo(f"Deleted {key}.")
    asyncio.run(run())


if __name__ == '__main__':
    cli()
