SHELL := /bin/bash
.DEFAULT_GOAL := help

ESPPORT ?= /dev/ttyUSB0
PORT ?= $(ESPPORT)
IDF_DOCKER_IMAGE ?= espressif/idf:release-v5.5
IDF_IMAGE ?= $(IDF_DOCKER_IMAGE)
IDF ?= $(shell command -v idf.py 2>/dev/null)
NPM ?= npm
CMAKE ?= cmake
PYTHON ?= python3

.PHONY: help bootstrap build build-native build-docker flash flash-docker monitor \
	test test-host test-web test-e2e e2e hil web web-dev mock sim fmt fmt-check \
	lint docs assets tools-deps check clean

help:
	@echo "Kaffeekasse targets:"
	@echo "  bootstrap     check tools and install web dependencies"
	@echo "  build         build firmware (local idf.py or Docker)"
	@echo "  assets        embed web/dist into the firmware (tools/embed_web.py)"
	@echo "  flash         flash $(PORT)"
	@echo "  monitor       serial monitor"
	@echo "  web           build admin SPA"
	@echo "  web-dev       Vite + mock API"
	@echo "  sim           build and run the LVGL simulator (provided by T-004)"
	@echo "  test          host + web tests"
	@echo "  e2e           Playwright admin UI tests"
	@echo "  hil           hardware-in-the-loop smoke test ($(PORT))"
	@echo "  check         format, lint, docs, tests, builds, size budgets"
	@echo "  fmt           clang-format C++; TypeScript is typechecked"
	@echo "  tools-deps    install Python HIL/lint dependencies"
	@echo "  clean         remove build output"

bootstrap:
	@bash tools/bootstrap.sh
	@$(PYTHON) -c 'import yaml' 2>/dev/null || \
		echo "warn: PyYAML missing; run 'make tools-deps' for OpenAPI lint/HIL"

tools-deps:
	$(PYTHON) -m pip install -r tools/requirements-tools.txt

web/node_modules: web/package-lock.json
	$(NPM) --prefix web ci

web: web/node_modules
	$(NPM) --prefix web run build

assets: web
	$(PYTHON) tools/embed_web.py

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
	$(PYTHON) tools/hil.py --port $(PORT)

build-native: assets
	@if [ -z "$(IDF)" ]; then echo "idf.py not found; use 'make build-docker' or install ESP-IDF 5.5"; exit 1; fi
	cd firmware && idf.py build

build-docker: assets
	docker run --rm -v "$(CURDIR)/firmware:/project" -w /project $(IDF_IMAGE) \
		bash -lc '. $$IDF_PATH/export.sh && idf.py build'

build:
	@if [ -n "$(IDF)" ]; then $(MAKE) build-native; else $(MAKE) build-docker; fi

flash:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) flash; else \
		docker run --rm -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) \
			$(IDF_IMAGE) bash -lc '. $$IDF_PATH/export.sh && idf.py -p $(PORT) flash'; fi

flash-docker: flash

monitor:
	@if [ -n "$(IDF)" ]; then cd firmware && idf.py -p $(PORT) monitor; else \
		docker run --rm -it -v "$(CURDIR)/firmware:/project" -w /project --device=$(PORT) \
			$(IDF_IMAGE) bash -lc '. $$IDF_PATH/export.sh && idf.py -p $(PORT) monitor'; fi

sim:
	$(CMAKE) -S sim -B sim/build
	$(CMAKE) --build sim/build -j
	./sim/build/kaffeekasse-sim

fmt:
	@if command -v clang-format >/dev/null; then \
		find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/managed_components' -o -path 'firmware/components/webassets/generated' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null | xargs -r clang-format -i; \
	fi
	$(NPM) --prefix web run typecheck

fmt-check:
	@if command -v clang-format >/dev/null; then \
		files=$$(find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/managed_components' -o -path 'firmware/components/webassets/generated' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null); \
		clang-format --dry-run --Werror $$files; \
	else echo "clang-format not installed, skipping"; fi

lint:
	$(PYTHON) tools/lint_openapi.py docs/api/openapi.yaml
	$(NPM) --prefix web run typecheck

docs:
	$(PYTHON) tools/check_docs.py

check: fmt-check lint docs test web assets
	@if command -v idf.py >/dev/null; then cd firmware && idf.py build; else echo "Skipping firmware build (idf.py not found; CI builds it in Docker)"; fi
	@$(PYTHON) tools/check_sizes.py

clean:
	rm -rf firmware/build tests/host/build sim/build web/dist web/node_modules \
		firmware/components/webassets/generated
