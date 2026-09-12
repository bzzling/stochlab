/// <reference lib="webworker" />
type Module = {
  ccall: (
    name: string,
    result: string,
    args: string[],
    values: string[],
  ) => string;
};
let engine: Promise<Module> | undefined;
async function load(): Promise<Module> {
  // This ES module is an Emscripten build artifact, loaded at runtime from public/.
  const moduleURL = new URL('/wasm/stochlab.mjs', self.location.origin).href;
  const { default: create } = await import(/* @vite-ignore */ moduleURL);
  return create({
    locateFile: (name: string) =>
      new URL(`/wasm/${name}`, self.location.origin).href,
  });
}
self.onmessage = async (
  event: MessageEvent<{ id: number; params: Record<string, unknown> }>,
) => {
  const { id, params } = event.data;
  try {
    engine ??= load();
    const module = await engine;
    if (params.action === 'ready') {
      self.postMessage({ id, result: { ready: true } });
      return;
    }
    const begin = performance.now();
    const raw = module.ccall(
      'sl_request',
      'string',
      ['string'],
      [JSON.stringify(params)],
    );
    const result = JSON.parse(raw);
    if (result.error) throw new Error(result.error);
    self.postMessage({ id, result, roundtrip_ms: performance.now() - begin });
  } catch (error) {
    self.postMessage({
      id,
      error: error instanceof Error ? error.message : String(error),
    });
  }
};
