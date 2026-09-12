#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v emcmake >/dev/null 2>&1; then
  echo 'Emscripten is required. Run: source /path/to/emsdk/emsdk_env.sh' >&2
  exit 1
fi
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
mkdir -p public/wasm
cp build-wasm/stochlab.mjs build-wasm/stochlab.wasm public/wasm/
