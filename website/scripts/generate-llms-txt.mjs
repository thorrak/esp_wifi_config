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
  'guides/esp-bus-events.md',
  'guides/http-server-sharing.md',
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
  'migration.md',
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

async function main() {
  // Read all doc files in order
  const pages = [];
  for (const relPath of PAGE_ORDER) {
    const fullPath = join(DOCS_DIR, relPath);
    if (!existsSync(fullPath)) {
      console.warn(`Warning: ${relPath} not found, skipping`);
      continue;
    }
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
