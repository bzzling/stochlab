import { defineConfig, devices } from '@playwright/test';
export default defineConfig({
  testDir:'./tests',testMatch:'browser.spec.ts',fullyParallel:true,workers:3,
  timeout:30000,expect:{timeout:15000},reporter:'list',
  use:{...devices['Desktop Chrome'],baseURL:process.env.BASE_URL??'http://127.0.0.1:4321',viewport:{width:1440,height:1000},screenshot:'only-on-failure',trace:'retain-on-failure'},
  webServer:process.env.BASE_URL?undefined:{command:'npm run preview',url:'http://127.0.0.1:4321',reuseExistingServer:true,timeout:30000},
});
