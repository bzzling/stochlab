import test from 'node:test';
import assert from 'node:assert/strict';
import { parseCommand } from '../src/lib/commands.ts';

test('command parser covers navigation, simulation and exchange controls', () => {
  assert.deepEqual(parseCommand(':process hawkes'), {
    type: 'model',
    model: 'hawkes',
  });
  assert.deepEqual(parseCommand(' set barrier 1.2 '), {
    type: 'set',
    key: 'barrier',
    value: 1.2,
  });
  assert.deepEqual(parseCommand(':seed 4294967295'), {
    type: 'set',
    key: 'seed',
    value: 4294967295,
  });
  assert.deepEqual(parseCommand(':run'), { type: 'run' });
  assert.deepEqual(parseCommand('exchange start'), {
    type: 'exchange',
    action: 'start',
  });
  assert.deepEqual(parseCommand('flow hawkes'), {
    type: 'flow',
    value: 'hawkes',
  });
  assert.deepEqual(parseCommand('maker disable'), {
    type: 'maker',
    value: false,
  });
  assert.deepEqual(parseCommand('speed 4x'), { type: 'speed', value: 4 });
});
test('malformed commands and unsafe workloads fail explicitly', () => {
  for (const cmd of [
    '',
    'run extra',
    'process unknown',
    'set foo 1',
    'set paths 1.5',
    'set paths 1000001',
    'set paths NaN',
    'seed -1',
    'seed 4294967296',
    'speed 3x',
    'flow nonsense',
    'maker maybe',
    'set T 1 extra',
    'set barrier Infinity',
  ]) {
    assert.throws(() => parseCommand(cmd), undefined, cmd);
  }
});
