import { useRef, useState } from 'react';
export function CommandBar({
  onCommand,
  help,
  setHelp,
  exchange = false,
}: {
  onCommand: (text: string) => void;
  help: boolean;
  setHelp: (v: boolean) => void;
  exchange?: boolean;
}) {
  const [text, setText] = useState('');
  const history = useRef<string[]>([]),
    index = useRef(0);
  return (
    <div className="command-area">
      {help && (
        <div className="command-help">
          <div className="pane-heading">
            <span>COMMAND REFERENCE</span>
            <button
              type="button"
              onClick={() => setHelp(false)}
              aria-label="Close command reference"
            >
              ESC ×
            </button>
          </div>
          <div className="command-grid">
            {(exchange
              ? [
                  ['exchange start', 'Resume the event clock'],
                  ['exchange pause', 'Pause after the current batch'],
                  ['exchange reset', 'Reset book and RNG'],
                  ['flow hawkes', 'Reset with self-exciting arrivals'],
                  ['maker enable', 'Reset with a quoting agent'],
                  ['speed 4x', 'Process four batches per tick'],
                ]
              : [
                  ['process hawkes', 'Open a process or experiment'],
                  ['set barrier 1.2', 'Change a parameter'],
                  ['set paths 100000', 'Set Monte Carlo sample size'],
                  ['seed 42', 'Set a reproducible seed'],
                  ['run', 'Execute the experiment'],
                  ['reseed', 'Generate a new seed and run'],
                ]
            ).map(([cmd, desc]) => (
              <button
                type="button"
                key={cmd}
                onClick={() => {
                  setText(cmd);
                  document.getElementById('command-input')?.focus();
                }}
              >
                <code>:{cmd}</code>
                <span>{desc}</span>
              </button>
            ))}
          </div>
        </div>
      )}
      <form
        className="command-bar"
        onSubmit={(e) => {
          e.preventDefault();
          if (!text.trim()) return;
          onCommand(text);
          history.current.push(text);
          index.current = history.current.length;
          setText('');
        }}
      >
        <span className="prompt" aria-hidden="true">
          ❯
        </span>
        <input
          id="command-input"
          aria-label="Workbench command"
          autoComplete="off"
          spellCheck={false}
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder={
            exchange
              ? 'exchange start · flow hawkes · help'
              : 'set barrier 1.2 · process hawkes · help'
          }
          onKeyDown={(e) => {
            if (e.key === 'ArrowUp') {
              e.preventDefault();
              index.current = Math.max(0, index.current - 1);
              setText(history.current[index.current] ?? '');
            }
            if (e.key === 'ArrowDown') {
              e.preventDefault();
              index.current = Math.min(
                history.current.length,
                index.current + 1,
              );
              setText(history.current[index.current] ?? '');
            }
            if (e.key === 'Escape') {
              e.currentTarget.blur();
              setHelp(false);
            }
          }}
        />
        <button
          type="button"
          className="help-button"
          onClick={() => setHelp(!help)}
        >
          COMMANDS <kbd>?</kbd>
        </button>
      </form>
      <div className="shortcut-strip">
        <span>
          <kbd>SPACE</kbd> {exchange ? 'start / pause' : 'run'}
        </span>
        <span>
          <kbd>R</kbd> {exchange ? 'reset' : 'reseed'}
        </span>
        <span>
          <kbd>:</kbd> command
        </span>
        <span className="local-note">COMPUTED LOCALLY · C++20 / WASM</span>
      </div>
    </div>
  );
}
