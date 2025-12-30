# ESP WiFi Manager - Web UI

Modern, responsive web interface for ESP WiFi Manager.

## Quick Start

```bash
# Install dependencies
npm install

# Development server (with hot reload)
npm run dev

# Production build
npm run build
```

## Features

- Status display with signal strength
- Network scanning and connection
- Saved networks management
- Dark mode support (auto-detect)
- Mobile-first responsive design
- Lightweight (~25KB gzipped)

## Customization

### Change Theme

Edit `src/styles/variables.css`:

```css
:root {
  --color-primary: #3b82f6;    /* Change accent color */
  --color-bg: #f8fafc;         /* Background */
  --color-surface: #ffffff;    /* Card background */
}
```

### Add Custom Component

1. Create component in `src/components/`
2. Import in `App.tsx`
3. Rebuild: `npm run build`

### Replace UI Completely

1. Keep `src/api/client.ts` for API calls
2. Replace components as needed
3. Follow API types in `src/types.ts`

## Build for ESP32

After building, output files are in `dist/`:
- `index.html` (~1KB)
- `assets/app.js` (~20KB gzipped)
- `assets/style.css` (~3KB gzipped)

These files are embedded into firmware via CMakeLists.txt.

## Project Structure

```
frontend/
├── src/
│   ├── api/           # REST API client
│   ├── components/    # Preact components
│   │   └── ui/        # Reusable UI components
│   ├── hooks/         # Custom hooks
│   ├── styles/        # CSS
│   ├── types.ts       # TypeScript types
│   ├── App.tsx        # Main app
│   └── main.tsx       # Entry point
├── dist/              # Build output
├── package.json
├── vite.config.ts
└── tsconfig.json
```

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/wifi/status | WiFi status |
| GET | /api/wifi/scan | Scan networks |
| GET | /api/wifi/networks | Saved networks |
| POST | /api/wifi/networks | Add network |
| DELETE | /api/wifi/networks/:ssid | Delete network |
| POST | /api/wifi/connect | Connect |
| POST | /api/wifi/disconnect | Disconnect |
| POST | /api/wifi/factory_reset | Factory reset |
