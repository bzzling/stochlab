import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFile } from 'node:fs/promises';
import create from '../public/wasm/stochlab.mjs';

const module = await create({
  wasmBinary: await readFile(
    new URL('../public/wasm/stochlab.wasm', import.meta.url),
  ),
});
function call(params) {
  return JSON.parse(
    module.ccall('sl_request', 'string', ['string'], [JSON.stringify(params)]),
  );
}
function clean(result) {
  const r = structuredClone(result);
  delete r.elapsed_ms;
  return r;
}
function close(a, b, path = 'root') {
  if (typeof a === 'number' && typeof b === 'number')
    assert.ok(
      Math.abs(a - b) <= 1e-8 * Math.max(1, Math.abs(a), Math.abs(b)),
      `${path}: ${a} != ${b}`,
    );
  else if (a && b && typeof a === 'object' && typeof b === 'object') {
    assert.deepEqual(Object.keys(a), Object.keys(b), path);
    for (const key of Object.keys(a)) close(a[key], b[key], `${path}.${key}`);
  } else assert.deepEqual(a, b, path);
}
for (const model of [
  'random-walk',
  'brownian',
  'first-passage',
  'reflection',
  'qv',
  'ou',
  'stationarity',
  'poisson',
  'compound-poisson',
  'ctmc',
  'hawkes',
]) {
  test(`WASM/native parity and repeatability: ${model}`, () => {
    const params = {
      action: 'simulate',
      model,
      paths: 200,
      steps: 128,
      T: 2,
      seed: 42,
    };
    const result = call(params);
    assert.equal(result.error, undefined);
    assert.deepEqual(clean(result), clean(call(params)));
    const native = JSON.parse(
      execFileSync(
        './build/stochlab',
        [
          'simulate',
          model,
          '--paths',
          '200',
          '--steps',
          '128',
          '--T',
          '2',
          '--seed',
          '42',
          '--json',
        ],
        { encoding: 'utf8' },
      ),
    );
    close(clean(result), clean(native));
    assert.equal(
      result.histogram.reduce((s, b) => s + b.count, 0),
      200,
    );
  });
}
test('WASM boundary rejects invalid requests and recovers for next request', () => {
  for (const params of [
    { action: 'unknown' },
    { action: 'simulate', model: 'hawkes', alpha: 2, beta: 1 },
    { action: 'simulate', paths: -1 },
    {
      action: 'simulate',
      model: 'ctmc',
      Q: [
        [1, 0],
        [0, -1],
      ],
    },
    { action: 'exchange-init', seed: 1.5 },
  ])
    assert.equal(typeof call(params).error, 'string');
  assert.equal(
    call({ action: 'simulate', model: 'brownian', paths: 10 }).paths,
    10,
  );
});
for (const flow of ['poisson', 'hawkes']) {
  test(`WASM exchange accounting, seeded native parity and batching: ${flow}`, () => {
    call({ action: 'exchange-init', seed: 42, flow, maker: true });
    const whole = call({ action: 'exchange-step', events: 2000 });
    call({ action: 'exchange-init', seed: 42, flow, maker: true });
    call({ action: 'exchange-step', events: 1200 });
    assert.deepEqual(whole, call({ action: 'exchange-step', events: 800 }));
    assert.ok(
      Math.abs(
        whole.maker.pnl -
          (whole.maker.cash + whole.maker.inventory * whole.mid),
      ) < 1e-8,
    );
    assert.ok(whole.bids[0].price < whole.asks[0].price);
    const native = JSON.parse(
      execFileSync(
        './build/stochlab',
        [
          'exchange',
          '--events',
          '2000',
          '--seed',
          '42',
          '--flow',
          flow,
          '--json',
        ],
        { encoding: 'utf8' },
      ),
    );
    close(whole, native);
  });
}
test('exchange C ABI handles batches above the internal per-call cap', () => {
  call({ action: 'exchange-init', seed: 42, maker: false });
  assert.equal(
    call({ action: 'exchange-step', events: 250001 }).events,
    250001,
  );
});
