.PHONY: setup native test validate bench wasm dev build check clean
setup:
	npm ci
native:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
test: native
	ctest --test-dir build --output-on-failure
validate: native
	./build/stochlab_tests statistics
bench: native
	./build/stochlab bench
wasm:
	npm run build:wasm
dev:
	npm run dev
build:
	npm run build
check:
	npm run check
	npm test
	npm run test:browser
clean:
	cmake -E remove_directory build
	cmake -E remove_directory build-wasm
	cmake -E remove_directory dist
