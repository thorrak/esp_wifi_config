#!/usr/bin/env node

/**
 * Generates llms.txt and llms-full.txt from the Docusaurus docs.
 * Run as a postbuild step: "postbuild": "node scripts/generate-llms-txt.mjs"
 */

import { readdir, readFile, writeFile, mkdir } from 'fs/promises';
import { join, relative } from 'path';
import { existsSync } from 'fs';

const DOCS_DIR = new URL('../docs', import.meta.url).pathname;
const BUILD_DIR = new URL('../build', import.meta.url).pathname;
const SITE_URL = 'https://configwifi.com';

// Desired order for concatenation (sidebar order)
const PAGE_ORDER = [
  'overview.md',
  'getting-started.md',
  'ai-integration-guide.md',
  'guides/multi-network.md',
  'guides/custom-variables.md',
  'guides/events.md',
  'guides/http-server-sharing.md',
  'guides/custom-webui.md',
  'provisioning/modes.md',
  'provisioning/softap-captive-portal.md',
  'provisioning/ble-gatt.md',
  'provisioning/improv-wifi.md',
  'api/c-api.md',
  'api/rest-api.md',
  'api/ble-protocol.md',
  'api/cli-commands.md',
  'api/kconfig.md',
  'examples.md',
];

function stripFrontmatter(content) {
  const match = content.match(/^---\n[\s\S]*?\n---\n/);
  if (match) {
    return content.slice(match[0].length).trim();
  }
  return content.trim();
}

function extractFrontmatter(content) {
  const match = content.match(/^---\n([\s\S]*?)\n---/);
  if (!match) return {};
  const fm = {};
  for (const line of match[1].split('\n')) {
    const colonIdx = line.indexOf(':');
    if (colonIdx > 0) {
      const key = line.slice(0, colonIdx).trim();
      let val = line.slice(colonIdx + 1).trim();
      // Strip quotes
      if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'"))) {
        val = val.slice(1, -1);
      }
      fm[key] = val;
    }
  }
  return fm;
}

function pathToUrl(filePath) {
  // overview.md -> /docs (it has slug: /)
  if (filePath === 'overview.md') return `${SITE_URL}/docs`;
  // Remove .md, replace backslash
  const slug = filePath.replace(/\.md$/, '').replace(/\\/g, '/');
  return `${SITE_URL}/docs/${slug}`;
}

/**
 * PAGE_ORDER must name every doc, and every doc must exist.
 *
 * This list is hand-maintained and the docs tree is not, so they drift. When
 * `guides/esp-bus-events.md` was renamed to `guides/events.md` the generator
 * printed one line of warning, skipped the page, and exited zero -- so the
 * entire event API vanished from llms.txt and llms-full.txt, which exist for
 * no other purpose than to be the thing an AI coding tool reads. It stayed
 * gone, along with `guides/custom-webui.md`, which was never listed at all.
 * CI did not catch it: the build check asserts that llms.txt *exists*, not
 * that it is complete.
 *
 * So drift is now fatal in both directions. A page that is listed but absent
 * is a rename nobody propagated; a page present but unlisted is a new doc
 * nobody added. Neither is something to discover from a user asking why the
 * documentation told them to call a function that no longer exists.
 */
async function assertPageOrderMatchesDocs() {
  const onDisk = new Set();
  const walk = async (dir) => {
    for (const entry of await readdir(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) await walk(full);
      else if (entry.name.endsWith('.md')) onDisk.add(relative(DOCS_DIR, full));
    }
  };
  await walk(DOCS_DIR);

  const listed = new Set(PAGE_ORDER);
  const missing = PAGE_ORDER.filter((p) => !onDisk.has(p));
  const unlisted = [...onDisk].filter((p) => !listed.has(p)).sort();

  if (missing.length || unlisted.length) {
    const lines = ['PAGE_ORDER and docs/ disagree, so llms.txt would be incomplete:'];
    for (const p of missing) lines.push(`  listed but not on disk: ${p}  (renamed or deleted?)`);
    for (const p of unlisted) lines.push(`  on disk but not listed: ${p}  (add it to PAGE_ORDER)`);
    throw new Error(lines.join('\n'));
  }
}

async function main() {
  await assertPageOrderMatchesDocs();

  // Read all doc files in order
  const pages = [];
  for (const relPath of PAGE_ORDER) {
    const fullPath = join(DOCS_DIR, relPath);
    const content = await readFile(fullPath, 'utf-8');
    const fm = extractFrontmatter(content);
    const body = stripFrontmatter(content);
    pages.push({
      path: relPath,
      title: fm.title || relPath,
      description: fm.description || '',
      url: pathToUrl(relPath),
      body,
    });
  }

  // Generate llms.txt (short orientation)
  const llmsTxt = `# ESP WiFi Config

> WiFi configuration component for ESP-IDF with multi-network support, auto-reconnect, SoftAP captive portal, Web UI, CLI, BLE GATT, Improv WiFi, and REST API.

## Links

- Documentation: ${SITE_URL}/docs
- GitHub: https://github.com/thorrak/esp_wifi_config
- ESP Component Registry: https://components.espressif.com/components/thorrak/esp_wifi_config
- Full AI-readable docs: ${SITE_URL}/llms-full.txt

## Table of Contents

${pages.map(p => `- [${p.title}](${p.url}): ${p.description}`).join('\n')}
`;

  // Generate llms-full.txt (all content concatenated)
  const sections = pages.map(p => {
    return `${'='.repeat(80)}
${p.title}
Source: ${p.url}
${'='.repeat(80)}

${p.body}`;
  });

  const llmsFullTxt = `# ESP WiFi Config — Complete Documentation

> This file contains the complete documentation for ESP WiFi Config, an ESP-IDF
> component for WiFi configuration with multi-network support, auto-reconnect,
> and multiple provisioning interfaces.
>
> Website: ${SITE_URL}
> GitHub: https://github.com/thorrak/esp_wifi_config
> Component Registry: https://components.espressif.com/components/thorrak/esp_wifi_config

${sections.join('\n\n')}
`;

  // Ensure build directory exists
  if (!existsSync(BUILD_DIR)) {
    await mkdir(BUILD_DIR, { recursive: true });
  }

  await writeFile(join(BUILD_DIR, 'llms.txt'), llmsTxt, 'utf-8');
  await writeFile(join(BUILD_DIR, 'llms-full.txt'), llmsFullTxt, 'utf-8');

  console.log(`Generated llms.txt (${llmsTxt.length} bytes)`);
  console.log(`Generated llms-full.txt (${llmsFullTxt.length} bytes)`);
}

main().catch(err => {
  console.error('Failed to generate llms.txt:', err);
  process.exit(1);
});
