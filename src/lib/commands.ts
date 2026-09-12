import { models, fieldInfo } from './models.ts';
export type Command =
  | { type: 'run' | 'reseed' | 'help' }
  | { type: 'model'; model: string }
  | { type: 'set'; key: string; value: number }
  | { type: 'exchange'; action: 'start' | 'pause' | 'reset' }
  | { type: 'flow'; value: 'poisson' | 'hawkes' }
  | { type: 'maker'; value: boolean }
  | { type: 'speed'; value: number };
export function parseCommand(text: string): Command {
  const words = text.trim().replace(/^:/, '').trim().split(/\s+/);
  const [verb, a, b, ...extra] = words;
  if (extra.length)
    throw new Error('Too many arguments. Type help for commands.');
  if (['run', 'reseed', 'help'].includes(verb) && !a)
    return { type: verb as 'run' | 'reseed' | 'help' };
  if (verb === 'process' && a && !b) {
    if (!models.some((m) => m.id === a))
      throw new Error(`Unknown process: ${a}`);
    return { type: 'model', model: a };
  }
  if ((verb === 'seed' && a && !b) || (verb === 'set' && a && b)) {
    const key = verb === 'seed' ? 'seed' : a,
      raw = verb === 'seed' ? a : b;
    if (key !== 'seed' && !Object.hasOwn(fieldInfo, key))
      throw new Error(`Unknown parameter: ${key}`);
    const value = Number(raw);
    if (!Number.isFinite(value))
      throw new Error('A finite numeric value is required.');
    if (['seed', 'paths', 'steps'].includes(key) && !Number.isInteger(value))
      throw new Error(`${key} must be an integer.`);
    if (key === 'seed' && (value < 0 || value > 4294967295))
      throw new Error('Seed must be in [0, 4294967295].');
    if (
      key !== 'seed' &&
      (value < fieldInfo[key].min || value > fieldInfo[key].max)
    )
      throw new Error(
        `${key} must be in [${fieldInfo[key].min}, ${fieldInfo[key].max}].`,
      );
    return { type: 'set', key, value };
  }
  if (verb === 'exchange' && ['start', 'pause', 'reset'].includes(a) && !b)
    return { type: 'exchange', action: a as 'start' | 'pause' | 'reset' };
  if (verb === 'flow' && ['poisson', 'hawkes'].includes(a) && !b)
    return { type: 'flow', value: a as 'poisson' | 'hawkes' };
  if (verb === 'maker' && ['enable', 'disable'].includes(a) && !b)
    return { type: 'maker', value: a === 'enable' };
  if (verb === 'speed' && a && !b) {
    const value = Number(a.replace(/x$/, ''));
    if (![1, 2, 4, 8].includes(value))
      throw new Error('Speed must be 1x, 2x, 4x or 8x.');
    return { type: 'speed', value };
  }
  throw new Error('Unknown command. Type help to see the command reference.');
}
