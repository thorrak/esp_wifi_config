import {themes as prismThemes} from 'prism-react-renderer';
import type {Config} from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'ESP WiFi Config',
  tagline: 'WiFi configuration component for ESP-IDF with multi-network support, auto-reconnect, and multiple provisioning interfaces',
  favicon: 'img/favicon.ico',

  future: {
    v4: true,
  },

  url: 'https://configwifi.com',
  baseUrl: '/',

  organizationName: 'thorrak',
  projectName: 'esp_wifi_config',

  onBrokenLinks: 'throw',

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          editUrl:
            'https://github.com/thorrak/esp_wifi_config/tree/main/website/',
        },
        blog: false, // Disable blog for now
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    image: 'img/social-card.jpg',
    colorMode: {
      respectPrefersColorScheme: true,
    },
    navbar: {
      title: 'ESP WiFi Config',
      // logo: {
      //   alt: 'ESP WiFi Config Logo',
      //   src: 'img/logo.svg',
      // },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docsSidebar',
          position: 'left',
          label: 'Docs',
        },
        {
          href: 'https://github.com/thorrak/esp_wifi_config',
          label: 'GitHub',
          position: 'right',
        },
        {
          href: 'https://components.espressif.com/components/thorrak/esp_wifi_config',
          label: 'Component Registry',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Docs',
          items: [
            {
              label: 'Getting Started',
              to: '/docs/getting-started',
            },
            {
              label: 'API Reference',
              to: '/docs/api/c-api',
            },
          ],
        },
        {
          title: 'More',
          items: [
            {
              label: 'GitHub',
              href: 'https://github.com/thorrak/esp_wifi_config',
            },
            {
              label: 'ESP Component Registry',
              href: 'https://components.espressif.com/components/thorrak/esp_wifi_config',
            },
          ],
        },
      ],
      copyright: `Copyright © ${new Date().getFullYear()} ESP WiFi Config Contributors. Built with Docusaurus.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
      additionalLanguages: ['c', 'bash', 'json', 'yaml'],
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
