import test from 'node:test';
import assert from 'node:assert/strict';
import { plotCrossing, reflectedPolyline } from '../src/lib/reflection.ts';
test('reflection meets the barrier continuously and preserves the original prefix', () => {
  const original = { t: [0, 0.5, 1], y: [0, 1.5, 2], hit: 1 };
  assert.equal(plotCrossing(original, 1), 1 / 3);
  assert.deepEqual(reflectedPolyline(original, 1), {
    t: [0, 1 / 3, 0.5, 1],
    y: [0, 1, 0.5, 0],
  });
  assert.deepEqual(original.y, [0, 1.5, 2]);
  assert.equal(plotCrossing({ ...original, hit: null }, 1), null);
  assert.equal(plotCrossing({ ...original, hit: 0 }, 0), 0);
});
