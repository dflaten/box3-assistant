SHELL := /bin/bash

PORT ?= /dev/ttyACM0
FISH_RUN = fish -lc 'cd $(CURDIR); get_idf; $(1)'

.PHONY: help format test build rebuild flash monitor deploy

help:
	@printf '%s\n' \
		'make format   Format all C/C++ sources with clang-format' \
		'make test     Run host-side unit tests' \
		'make build    Build the firmware with ESP-IDF' \
		'make rebuild  Run a fullclean firmware build' \
		'make flash    Build and flash to $(PORT)' \
		'make monitor  Open the serial monitor on $(PORT)' \
		'make deploy   Build, flash, and monitor on $(PORT)'

format:
	./scripts/format.sh

test:
	./tests/run_unit_tests.sh

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
