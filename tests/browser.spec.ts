import { test, expect } from '@playwright/test';
import { mkdir } from 'node:fs/promises';
const routes = [
  '/',
  '/processes/brownian/',
  '/processes/random-walk/',
  '/processes/ou/',
  '/processes/poisson/',
  '/processes/compound-poisson/',
  '/processes/ctmc/',
  '/processes/hawkes/',
  '/experiments/reflection/',
  '/experiments/qv/',
  '/experiments/stationarity/',
];
test('million-path worker run and keyboard reseeding', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await page.getByLabel('Realizations').fill('1000000');
  await page.locator('h1').click();
  await page.keyboard.press('Space');
  await expect(page.locator('.validation-pane .pane-heading')).toContainText(
    '1,000,000',
  );
  const seed = await page.getByLabel('RNG seed').inputValue();
  await page.keyboard.press('r');
  await expect(page.getByLabel('RNG seed')).not.toHaveValue(seed);
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
});
test('documentation screenshots use actual browser simulation results', async ({
  page,
}) => {
  await mkdir('docs/screenshots', { recursive: true });
  for (const [route, name] of [
    ['/', 'first-passage'],
    ['/processes/hawkes/', 'hawkes'],
    ['/systems/exchange/', 'exchange'],
  ]) {
    await page.goto(route);
    await expect(page.getByTestId('engine-status')).toContainText('WASM');
    if (name === 'exchange') {
      await page.getByLabel('Simulation speed').selectOption('8');
      await page.getByRole('button', { name: 'START' }).click();
      await expect
        .poll(async () =>
          Number(
            (await page.getByTestId('events').innerText()).replaceAll(',', ''),
          ),
        )
        .toBeGreaterThan(1000);
      await page.getByRole('button', { name: 'PAUSE' }).click();
    }
    await page.screenshot({
      path: `docs/screenshots/${name}.png`,
      fullPage: true,
    });
  }
});
for (const route of routes)
  test(`WASM loads on fresh deep link ${route}`, async ({ page }) => {
    const errors: string[] = [];
    page.on('pageerror', (e) => errors.push(e.message));
    page.on('console', (msg) => {
      if (msg.type() === 'error') errors.push(msg.text());
    });
    await page.goto(route);
    await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
    await expect(page.locator('.validation-table tbody tr')).not.toHaveCount(0);
    await expect(page.locator('canvas')).toBeVisible();
    await expect(page.getByRole('alert')).toHaveCount(0);
    expect(errors).toEqual([]);
  });
test('commands, rerun reproducibility, selected reflection, keyboard and export', async ({
  page,
}) => {
  await page.goto('/experiments/reflection/');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  const before = await page.locator('.validation-table tbody').innerText();
  await page.getByRole('button', { name: 'RUN EXPERIMENT' }).click();
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  expect(await page.locator('.validation-table tbody').innerText()).toEqual(
    before,
  );
  await page.getByRole('button', { name: 'Next crossing' }).click();
  await page.getByRole('button', { name: 'Reflect tail' }).click();
  await expect(
    page.getByRole('button', { name: 'Reflect tail' }),
  ).toHaveAttribute('aria-pressed', 'false');
  await page.getByLabel('Workbench command').fill(':set barrier 1.2');
  await page.getByLabel('Workbench command').press('Enter');
  await expect(page.getByLabel('Barrier · a')).toHaveValue('1.2');
  await page.getByLabel('Workbench command').fill(':run');
  await page.getByLabel('Workbench command').press('Enter');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await expect(page.locator('.validation-table tbody')).not.toHaveText(before);
  await page.getByLabel('Workbench command').fill('set paths nonsense');
  await page.getByLabel('Workbench command').press('Enter');
  await expect(page.getByRole('alert')).toContainText('finite');
  await page.getByRole('button', { name: 'Dismiss error' }).click();
  await page.getByLabel('Workbench command').press('Escape');
  await page.locator('h1').click();
  await page.keyboard.press(':');
  await expect(page.getByLabel('Workbench command')).toBeFocused();
  await page.getByLabel('Workbench command').fill('help');
  await page.getByLabel('Workbench command').press('Enter');
  await expect(
    page.getByText('COMMAND REFERENCE', { exact: true }),
  ).toBeVisible();
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Export result' }).click();
  expect((await download).suggestedFilename()).toContain('stochlab-reflection');
});
test('invalid Hawkes and CTMC parameters fail then recover', async ({
  page,
}) => {
  await page.goto('/processes/hawkes/');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await page.getByLabel('Excitation · α', { exact: true }).fill('2');
  await page.getByRole('button', { name: 'RUN EXPERIMENT' }).click();
  await expect(page.getByRole('alert')).toContainText(/alpha|stable/);
  await page.getByLabel('Excitation · α', { exact: true }).fill('0.7');
  await page.getByRole('button', { name: 'RUN EXPERIMENT' }).click();
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await page.goto('/processes/ctmc/');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await page.getByLabel('Generator matrix Q').fill('[');
  await page.getByRole('button', { name: 'RUN EXPERIMENT' }).click();
  await expect(page.getByRole('alert')).toBeVisible();
  await page.getByLabel('Generator matrix Q').fill('[[-2,2],[1,-1]]');
  await page.getByRole('button', { name: 'RUN EXPERIMENT' }).click();
  await expect(page.getByTestId('engine-status')).toHaveText('WASM READY');
  await expect(page.locator('.occupation-row')).toHaveCount(2);
});
test('exchange steps, runs, pauses and switches stochastic flow', async ({
  page,
}) => {
  const errors: string[] = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto('/systems/exchange/');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM · PAUSED');
  await expect(page.getByTestId('events')).toHaveText('0');
  await page.getByRole('button', { name: 'Step +1' }).click();
  await expect(page.getByTestId('events')).toHaveText('1');
  await page.getByRole('button', { name: 'START' }).click();
  await expect(page.getByTestId('engine-status')).toHaveText('WASM · RUNNING');
  await expect
    .poll(async () =>
      Number(
        (await page.getByTestId('events').innerText()).replaceAll(',', ''),
      ),
    )
    .toBeGreaterThan(40);
  await page.getByRole('button', { name: 'PAUSE' }).click();
  await expect(page.getByTestId('engine-status')).toHaveText('WASM · PAUSED');
  await page.getByLabel('Arrival process').selectOption('hawkes');
  await page.getByRole('button', { name: 'Apply & reset' }).click();
  await expect(page.getByTestId('events')).toHaveText('0');
  await page.getByLabel('Workbench command').fill('exchange start');
  await page.getByLabel('Workbench command').press('Enter');
  await expect
    .poll(async () =>
      Number(
        (await page.getByTestId('events').innerText()).replaceAll(',', ''),
      ),
    )
    .toBeGreaterThan(40);
  await page.getByLabel('Workbench command').fill('exchange pause');
  await page.getByLabel('Workbench command').press('Enter');
  await expect(page.getByTestId('engine-status')).toHaveText('WASM · PAUSED');
  await expect(page.locator('.tape-table tbody tr')).not.toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);
  expect(errors).toEqual([]);
});
for (const width of [390, 768, 1280])
  test(`responsive layout remains usable at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 });
    for (const route of ['/', '/systems/exchange/']) {
      await page.goto(route);
      await expect(page.getByTestId('engine-status')).toContainText('WASM');
      expect(
        await page.evaluate(
          () => document.documentElement.scrollWidth <= innerWidth + 1,
        ),
      ).toBe(true);
      await expect(page.locator('.run-button').first()).toBeVisible();
    }
  });
test('WASM MIME and static documentation routes', async ({ request, page }) => {
  const wasm = await request.get('/wasm/stochlab.wasm');
  expect(wasm.status()).toBe(200);
  expect(wasm.headers()['content-type']).toContain('application/wasm');
  for (const route of ['/methods/', '/native/']) {
    await page.goto(route);
    await expect(page.locator('h1')).toBeVisible();
    expect(await page.locator('astro-island').count()).toBe(0);
  }
});
