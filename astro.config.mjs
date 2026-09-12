import { defineConfig } from 'astro/config';
import react from '@astrojs/react';

export default defineConfig({
  site: 'https://stochlab.brandonling.ca',
  integrations: [react()],
  output: 'static',
  trailingSlash: 'always',
  devToolbar: { enabled: false },
});
