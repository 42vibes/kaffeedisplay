SHELL := /bin/bash
.DEFAULT_GOAL := help

PORT ?= /dev/ttyUSB0
IDF_IMAGE ?= espressif/idf:release-v5.5
IDF ?= $(shell command -v idf.py 2>/dev/null)
NPM ?= npm
CMAKE ?= cmake

.PHONY: help bootstrap build build-native build-docker flash flash-docker monitor test test-host test-web test-e2e e2e hil web web-dev mock sim fmt fmt-check lint check clean

help:
	@echo "Kaffeekasse targets:"
	@echo "  bootstrap     check tools and install web dependencies"
	@echo "  build         build firmware (local idf.py or Docker)"
	@echo "  flash         flash $(PORT)"
	@echo "  monitor       serial monitor"
	@echo "  web           build admin SPA"
	@echo "  web-dev       Vite + mock API"
	@echo "  sim           build and run the LVGL simulator"
	@echo "  test          host + web tests"
	@echo "  check         format, lint, tests, builds, size budgets"
	@echo "  fmt           format C++ and TypeScript"
	@echo "  clean         remove build output"

bootstrap:
	@bash tools/bootstrap.sh
	$(NPM) --prefix web ci

web/node_modules: web/package-lock.json
	$(NPM) --prefix web ci

web: web/node_modules
	$(NPM) --prefix web run build

web-dev: web/node_modules
	@bash -c '$(NPM) --prefix web run mock & MOCK_PID=$$!; trap "kill $$MOCK_PID" EXIT; $(NPM) --prefix web run dev'

mock: web/node_modules
	$(NPM) --prefix web run mock

test-host:
	$(CMAKE) -S tests/host -B tests/host/build -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build tests/host/build -j
	./tests/host/build/kaffee_tests

test-web: web/node_modules
	$(NPM) --prefix web run typecheck
	$(NPM) --prefix web test

test: test-host test-web

test-e2e: web/node_modules
	$(NPM) --prefix web run e2e

e2e: test-e2e

hil:
	python3 tools/hil.py --port $(PORT)

build-native:
	@if [ -z "$(IDF)" ]; then echo "idf.py not found; use 'make build-docker' or install ESP-IDF 5.5"; exit 1; fi
	cd firmware && idf.py build

build-docker: web
	mkdir -p firmware/main/web_assets
	rm -rf firmware/main/web_assets/*
	cp -r web/dist/* firmware/main/web_assets/
	docker run --rm -v "$(CURDIR)/firmware:/project" -w /project $(IDF_IMAGE) idf.py build

build: web
	mkdir -p firmware/main/web_assets
	rm -rf firmware/main/web_assets/*
	cp -r web/dist/* firmware/main/web_assets/
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py build; else echo "idf.py not found, using Docker"; $(MAKE) build-docker; fi

flash:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) flash; else docker run --rm -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) $(IDF_IMAGE) idf.py -p $(PORT) flash; fi

monitor:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) monitor; else docker run --rm -it -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) $(IDF_IMAGE) idf.py -p $(PORT) monitor; fi

sim:
	$(CMAKE) -S sim -B sim/build
	$(CMAKE) --build sim/build -j
	./sim/build/kaffeekasse-sim

fmt:
	@if command -v clang-format >/dev/null; then \
		find firmware tests/host sim -path '*/third_party' -prune -o -path '*/build' -prune -o -path '*/managed_components' -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null | xargs -r clang-format -i; \
	fi
	$(NPM) --prefix web run typecheck

fmt-check:
	@if command -v clang-format >/dev/null; then \
		files=$$(find firmware tests/host sim -path '*/third_party' -prune -o -path '*/build' -prune -o -path '*/managed_components' -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null); \
		clang-format --dry-run --Werror $$files; \
	else echo "clang-format not installed, skipping"; fi

lint:
	python3 tools/lint_openapi.py docs/api/openapi.yaml
	$(NPM) --prefix web run typecheck

check: fmt-check lint test web
	@if command -v idf.py >/dev/null; then cd firmware && idf.py build; else echo "Skipping firmware build (idf.py not found; CI builds it in Docker)"; fi
	@python3 tools/check_sizes.py

clean:
	rm -rf firmware/build tests/host/build sim/build web/dist web/node_modules
