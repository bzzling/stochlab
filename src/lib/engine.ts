export class Engine {
  private worker: Worker;
  private next = 0;
  private pending = new Map<
    number,
    {
      resolve: (value: unknown) => void;
      reject: (reason: Error) => void;
      timer: ReturnType<typeof setTimeout>;
    }
  >();
  constructor() {
    this.worker = new Worker(new URL('./engine.worker.ts', import.meta.url), {
      type: 'module',
    });
    this.worker.onmessage = ({ data }) => {
      const job = this.pending.get(data.id);
      if (!job) return;
      clearTimeout(job.timer);
      this.pending.delete(data.id);
      if (data.error) job.reject(new Error(data.error));
      else job.resolve(data.result);
    };
    this.worker.onerror = () =>
      this.rejectAll(
        'The simulation worker failed. Reload to restart the engine.',
      );
  }
  call<T>(params: Record<string, unknown>): Promise<T> {
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(() => {
        this.worker.terminate();
        this.rejectAll(
          'Simulation exceeded 60 seconds. Reload and reduce the workload.',
        );
      }, 60000);
      this.pending.set(id, {
        resolve: resolve as (v: unknown) => void,
        reject,
        timer,
      });
      this.worker.postMessage({ id, params });
    });
  }
  private rejectAll(message: string) {
    for (const job of this.pending.values()) {
      clearTimeout(job.timer);
      job.reject(new Error(message));
    }
    this.pending.clear();
  }
  close() {
    this.worker.terminate();
    this.rejectAll('Engine closed.');
  }
}
