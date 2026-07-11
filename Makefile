SHELL := /bin/bash

PORT ?= /dev/ttyACM0
EXPECTED_IDF_VERSION ?= v6.0.2
IDF_EXPORT ?= $(HOME)/.espressif/v6.0.2/esp-idf/export.fish
FISH_RUN = fish -lc 'cd $(CURDIR); source $(IDF_EXPORT); set -l idf_version (idf.py --version); string match -q "*$(EXPECTED_IDF_VERSION)*" "$$idf_version"; or begin; echo "Expected ESP-IDF $(EXPECTED_IDF_VERSION), got $$idf_version" >&2; exit 1; end; $(1)'

.PHONY: help format test config-local build rebuild flash monitor deploy sdkconfig-from-main sdkconfig-to-main

help:
	@printf '%s\n' \
		'make format   Format all C/C++ sources with clang-format' \
		'make test     Run host-side unit tests' \
		'make config-local  Regenerate sdkconfig from defaults plus sdkconfig.defaults.local' \
		'make build    Build the firmware with ESP-IDF' \
		'make rebuild  Run a fullclean firmware build' \
		'make flash    Build and flash to $(PORT)' \
		'make monitor  Open the serial monitor on $(PORT)' \
		'make deploy   Build, flash, and monitor on $(PORT)' \
		'make sdkconfig-from-main  Copy sdkconfig from the main worktree into this worktree' \
		'make sdkconfig-to-main    Copy sdkconfig from this worktree back to the main worktree'

format:
	./scripts/format.sh

test:
	./tests/run_unit_tests.sh

config-local:
	$(call FISH_RUN,set -x SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local"; rm -f sdkconfig sdkconfig.old; idf.py reconfigure)

build:
	$(call FISH_RUN,idf.py build)

rebuild:
	$(call FISH_RUN,idf.py fullclean build)

flash:
	$(call FISH_RUN,idf.py -p $(PORT) build flash)

monitor:
	$(call FISH_RUN,idf.py -p $(PORT) monitor)

deploy:
	$(call FISH_RUN,idf.py -p $(PORT) build flash monitor)

sdkconfig-from-main:
	./scripts/sync-sdkconfig.sh from-main

sdkconfig-to-main:
	./scripts/sync-sdkconfig.sh to-main
