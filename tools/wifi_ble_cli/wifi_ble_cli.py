#!/usr/bin/env python3
"""
ESP WiFi Manager BLE CLI Client

A command-line tool to configure ESP32 WiFi settings over BLE.
"""

import asyncio
import json
import sys
from typing import Optional

import click
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

# BLE UUIDs (must match esp_wifi_manager_ble.c)
SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb"
CHAR_STATUS_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb"
CHAR_COMMAND_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb"
CHAR_RESPONSE_UUID = "0000ffe3-0000-1000-8000-00805f9b34fb"

# Response buffer
response_data = bytearray()
response_complete = asyncio.Event()


def notification_handler(sender, data):
    """Handle notifications from Response characteristic."""
    global response_data
    response_data.extend(data)
    # Check if response is complete (ends with })
    try:
        text = response_data.decode('utf-8')
        if text.strip().endswith('}'):
            response_complete.set()
    except UnicodeDecodeError:
        pass


async def find_device(name_prefix: str = "ESP32-WiFi") -> Optional[BLEDevice]:
    """Scan for ESP WiFi Manager device."""
    click.echo(f"Scanning for BLE devices with prefix '{name_prefix}'...")

    devices = await BleakScanner.discover(timeout=10.0)
    for device in devices:
        if device.name and device.name.startswith(name_prefix):
            click.echo(f"Found: {device.name} ({device.address})")
            return device

    return None


async def send_command(client: BleakClient, cmd: dict, timeout: float = 10.0) -> dict:
    """Send command and wait for response."""
    global response_data, response_complete

    response_data = bytearray()
    response_complete.clear()

    # Subscribe to response notifications
    await client.start_notify(CHAR_RESPONSE_UUID, notification_handler)

    # Send command
    cmd_json = json.dumps(cmd)
    await client.write_gatt_char(CHAR_COMMAND_UUID, cmd_json.encode('utf-8'))

    # Wait for response
    try:
        await asyncio.wait_for(response_complete.wait(), timeout=timeout)
    except asyncio.TimeoutError:
        await client.stop_notify(CHAR_RESPONSE_UUID)
        raise click.ClickException("Command timeout")

    await client.stop_notify(CHAR_RESPONSE_UUID)

    # Parse response
    try:
        return json.loads(response_data.decode('utf-8'))
    except json.JSONDecodeError as e:
        raise click.ClickException(f"Invalid JSON response: {e}")


def print_json(data: dict):
    """Pretty print JSON data."""
    click.echo(json.dumps(data, indent=2))


@click.group()
@click.option('--device', '-d', help='Device address (MAC or UUID)')
@click.option('--name', '-n', default='ESP32-WiFi', help='Device name prefix to scan for')
@click.pass_context
def cli(ctx, device, name):
    """ESP WiFi Manager BLE CLI Client"""
    ctx.ensure_object(dict)
    ctx.obj['device'] = device
    ctx.obj['name'] = name


async def connect_and_run(ctx, cmd: dict, timeout: float = 10.0):
    """Connect to device and run command."""
    device_addr = ctx.obj.get('device')
    name_prefix = ctx.obj.get('name', 'ESP32-WiFi')

    if not device_addr:
        device = await find_device(name_prefix)
        if not device:
            raise click.ClickException(f"No device found with prefix '{name_prefix}'")
        device_addr = device.address

    click.echo(f"Connecting to {device_addr}...")

    async with BleakClient(device_addr) as client:
        if not client.is_connected:
            raise click.ClickException("Failed to connect")

        click.echo("Connected!")
        response = await send_command(client, cmd, timeout)
        return response


@cli.command()
@click.pass_context
def status(ctx):
    """Get WiFi connection status."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "get_status"})
        if resp.get("status") == "ok":
            data = resp.get("data", {})
            state = data.get("state", "unknown")
            click.echo(f"State: {state}")
            if state == "connected":
                click.echo(f"SSID: {data.get('ssid', 'N/A')}")
                click.echo(f"IP: {data.get('ip', 'N/A')}")
                click.echo(f"RSSI: {data.get('rssi', 'N/A')} dBm")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.pass_context
def scan(ctx):
    """Scan for available WiFi networks."""
    async def run():
        click.echo("Scanning WiFi networks (this may take a few seconds)...")
        resp = await connect_and_run(ctx, {"cmd": "scan"}, timeout=15.0)
        if resp.get("status") == "ok":
            networks = resp.get("data", {}).get("networks", [])
            if not networks:
                click.echo("No networks found")
                return

            click.echo(f"\nFound {len(networks)} network(s):\n")
            click.echo(f"{'SSID':<32} {'RSSI':>6} {'CH':>4} {'Security':<12}")
            click.echo("-" * 60)
            for net in networks:
                ssid = net.get("ssid", "")[:32]
                rssi = net.get("rssi", 0)
                channel = net.get("channel", 0)
                auth = net.get("auth", "unknown")
                click.echo(f"{ssid:<32} {rssi:>6} {channel:>4} {auth:<12}")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('list')
@click.pass_context
def list_networks(ctx):
    """List saved WiFi networks."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "list_networks"})
        if resp.get("status") == "ok":
            networks = resp.get("data", {}).get("networks", [])
            if not networks:
                click.echo("No saved networks")
                return

            click.echo(f"\nSaved networks ({len(networks)}):\n")
            for i, net in enumerate(networks):
                ssid = net.get("ssid", "")
                priority = net.get("priority", 0)
                click.echo(f"  {i+1}. {ssid} (priority: {priority})")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.argument('ssid')
@click.argument('password')
@click.option('--priority', '-p', default=0, help='Network priority (higher = preferred)')
@click.pass_context
def add(ctx, ssid, password, priority):
    """Add a WiFi network."""
    async def run():
        cmd = {
            "cmd": "add_network",
            "params": {
                "ssid": ssid,
                "password": password,
                "priority": priority
            }
        }
        resp = await connect_and_run(ctx, cmd)
        if resp.get("status") == "ok":
            click.echo(f"Network '{ssid}' added successfully")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.argument('ssid')
@click.option('--password', '-pw', default=None, help='New password for the network')
@click.option('--priority', '-p', default=None, type=int, help='New priority (higher = preferred)')
@click.pass_context
def update(ctx, ssid, password, priority):
    """Update password or priority of a saved WiFi network."""
    async def run():
        params = {"ssid": ssid}
        if password is not None:
            params["password"] = password
        if priority is not None:
            params["priority"] = priority
        resp = await connect_and_run(ctx, {"cmd": "update_network", "params": params})
        if resp.get("status") == "ok":
            click.echo(f"Network '{ssid}' updated successfully")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.argument('ssid')
@click.pass_context
def delete(ctx, ssid):
    """Delete a saved WiFi network."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "del_network", "params": {"ssid": ssid}})
        if resp.get("status") == "ok":
            click.echo(f"Network '{ssid}' deleted")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.argument('ssid', required=False)
@click.pass_context
def connect(ctx, ssid):
    """Connect to a WiFi network."""
    async def run():
        cmd = {"cmd": "connect"}
        if ssid:
            cmd["params"] = {"ssid": ssid}

        resp = await connect_and_run(ctx, cmd)
        if resp.get("status") == "ok":
            click.echo("Connect initiated" + (f" to '{ssid}'" if ssid else ""))
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command()
@click.pass_context
def disconnect(ctx):
    """Disconnect from WiFi."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "disconnect"})
        if resp.get("status") == "ok":
            click.echo("Disconnected")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('ap-status')
@click.pass_context
def ap_status(ctx):
    """Get AP mode status."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "get_ap_status"})
        if resp.get("status") == "ok":
            data = resp.get("data", {})
            active = data.get("active", False)
            click.echo(f"AP Active: {active}")
            if active:
                click.echo(f"SSID: {data.get('ssid', 'N/A')}")
                click.echo(f"IP: {data.get('ip', 'N/A')}")
                click.echo(f"Clients: {data.get('clients', 0)}")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('start-ap')
@click.pass_context
def start_ap(ctx):
    """Start AP mode."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "start_ap"})
        if resp.get("status") == "ok":
            click.echo("AP mode started")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('stop-ap')
@click.pass_context
def stop_ap(ctx):
    """Stop AP mode."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "stop_ap"})
        if resp.get("status") == "ok":
            click.echo("AP mode stopped")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('get-var')
@click.argument('key')
@click.pass_context
def get_var(ctx, key):
    """Get a custom variable."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "get_var", "params": {"key": key}})
        if resp.get("status") == "ok":
            value = resp.get("data", {}).get("value", "")
            click.echo(f"{key}={value}")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('set-var')
@click.argument('key')
@click.argument('value')
@click.pass_context
def set_var(ctx, key, value):
    """Set a custom variable."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "set_var", "params": {"key": key, "value": value}})
        if resp.get("status") == "ok":
            click.echo(f"Variable '{key}' set")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('list-vars')
@click.pass_context
def list_vars(ctx):
    """List all custom variables."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "list_vars"})
        if resp.get("status") == "ok":
            variables = resp.get("data", {}).get("vars", [])
            if not variables:
                click.echo("No variables set")
                return

            click.echo(f"\nCustom variables ({len(variables)}):\n")
            for var in variables:
                click.echo(f"  {var.get('key')}={var.get('value')}")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('del-var')
@click.argument('key')
@click.pass_context
def del_var(ctx, key):
    """Delete a custom variable."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "del_var", "params": {"key": key}})
        if resp.get("status") == "ok":
            click.echo(f"Variable '{key}' deleted")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('factory-reset')
@click.confirmation_option(prompt='Are you sure you want to factory reset?')
@click.pass_context
def factory_reset(ctx):
    """Factory reset all settings."""
    async def run():
        resp = await connect_and_run(ctx, {"cmd": "factory_reset"})
        if resp.get("status") == "ok":
            click.echo("Factory reset complete. Device will restart.")
        else:
            click.echo(f"Error: {resp.get('error', 'Unknown error')}")

    asyncio.run(run())


@cli.command('devices')
def list_devices():
    """Scan and list all ESP WiFi Manager BLE devices."""
    async def run():
        click.echo("Scanning for BLE devices...")
        devices = await BleakScanner.discover(timeout=10.0)

        esp_devices = [d for d in devices if d.name and d.name.startswith("ESP32-WiFi")]

        if not esp_devices:
            click.echo("No ESP WiFi Manager devices found")
            return

        click.echo(f"\nFound {len(esp_devices)} device(s):\n")
        click.echo(f"{'Name':<30} {'Address':<40}")
        click.echo("-" * 70)
        for device in esp_devices:
            click.echo(f"{device.name:<30} {device.address:<40}")

    asyncio.run(run())


if __name__ == '__main__':
    cli()
