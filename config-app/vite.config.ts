import { defineConfig } from 'vite';

// PAGES_BASE is set by the GitHub Pages workflow (e.g. /gaiaball/). Dev and e2e keep '/'.
export default defineConfig({
  base: process.env.PAGES_BASE ?? '/',
});
