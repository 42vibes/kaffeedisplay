SHELL := /bin/bash
.DEFAULT_GOAL := help

PORT ?= /dev/ttyUSB0
IDF_IMAGE ?= espressif/idf:release-v5.5
IDF ?= $(shell command -v idf.py 2>/dev/null)
NPM ?= npm
CMAKE ?= cmake
SCREENSHOT_DIR ?= docs/ui/screenshots
SCREENSHOT_CHECK_DIR ?= /tmp/kaffeekasse-screenshots

.PHONY: help bootstrap build build-native build-docker flash flash-docker monitor test test-host test-web test-e2e e2e hil web web-dev mock sim sim-build screenshots screenshots-check test-ui fmt fmt-check lint check clean

help:
	@echo "Kaffeekasse targets:"
	@echo "  bootstrap     check tools and install web dependencies"
	@echo "  build         build firmware (local idf.py or Docker)"
	@echo "  flash         flash $(PORT)"
	@echo "  monitor       serial monitor"
	@echo "  web           build admin SPA"
	@echo "  web-dev       Vite + mock API"
	@echo "  sim           build and run the LVGL simulator"
	@echo "  screenshots   regenerate docs/ui/screenshots from the simulator"
	@echo "  test-ui       LVGL click/hit-test smoke test (SDL2, no board)"
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

sim-build:
	$(CMAKE) -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build sim/build -j

sim: sim-build
	./sim/build/kaffeekasse-sim

screenshots: sim-build
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/01-idle.png --page 1 --state idle
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/02-user-selected.png --page 1 --state selected
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/03-booking-toast.png --page 1 --state toast
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/04-leaderboard.png --page 2 --state idle
	./sim/build/kaffeekasse-sim --screenshot $(SCREENSHOT_DIR)/05-system-ap-qr.png --page 3 --state ap

screenshots-check: sim-build
	rm -rf $(SCREENSHOT_CHECK_DIR)
	mkdir -p $(SCREENSHOT_CHECK_DIR)
	$(MAKE) screenshots SCREENSHOT_DIR=$(SCREENSHOT_CHECK_DIR)
	python3 tools/check_screenshots.py docs/ui/screenshots $(SCREENSHOT_CHECK_DIR)

test-ui: sim-build
	./sim/build/kaffeekasse-sim --click-test

fmt:
	@if command -v clang-format >/dev/null; then \
		find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/_deps' -o -path '*/managed_components' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null | xargs -r clang-format -i; \
	fi
	$(NPM) --prefix web run typecheck

fmt-check:
	@if command -v clang-format >/dev/null; then \
		files=$$(find firmware tests/host sim \( -path '*/third_party' -o -path '*/build' -o -path '*/_deps' -o -path '*/managed_components' \) -prune -o \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print 2>/dev/null); \
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
