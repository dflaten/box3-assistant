# AGENTS.md

## ESP-IDF Environment

This project expects the ESP-IDF tools to be activated from `fish`.

Use this before running `idf.py` commands:

```fish
cd /home/<user-name>/projects/esp-projects/box3-assistant
get_idf
```

`get_idf` is defined in the user's `fish` config and sets up the ESP-IDF environment for this shell session.

## Common Commands

Build:

```fish
get_idf
idf.py build
```

Clean rebuild:

```fish
get_idf
idf.py fullclean build
```

Flash and monitor on the ESP32-S3-BOX-3:

```fish
get_idf
idf.py -p /dev/ttyACM0 build flash monitor
```

Host-side unit tests:

```bash
./tests/run_unit_tests.sh
```

Or from the build tree:

```bash
cmake --build build --target unit-tests
```

## Notes

- Run `get_idf` in each new shell before using `idf.py`.
- If `idf.py` is not found, the environment is not active yet.
- This repo now uses a custom `partitions.csv`, so avoid deleting it when cleaning up build files.
- Run the host-side unit tests after changes to pure logic or formatting code, especially `assistant_state.c`, `assistant_command_text.c`, `weather_format.c`, or the unit-test harness itself.
- Run `idf.py build` after changes that affect firmware integration, ESP-IDF-facing code, task orchestration, or component wiring.
