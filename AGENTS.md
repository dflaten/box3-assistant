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

- When working in a git worktree, sync `sdkconfig` from the main checkout before firmware builds or config edits with `make sdkconfig-from-main`, but always ask the user for confirmation before running it because it overwrites the worktree copy.
- After the work is complete and the pull request has been published, copy the finished worktree `sdkconfig` back to the main checkout with `make sdkconfig-to-main`, but always ask the user for confirmation before running it because it overwrites the main checkout copy.
- Never run either `sdkconfig` sync direction automatically or blindly.
- Run `get_idf` in each new shell before using `idf.py`.
- If `idf.py` is not found, the environment is not active yet.
- Use the repo `.clang-format` for C/C++ formatting changes when `clang-format` is available locally.
- This repo now uses a custom `partitions.csv`, so avoid deleting it when cleaning up build files.
- Add documentation comments for new methods using the same Doxygen-style pattern already established in the codebase.
- Add or extend unit tests for newly added methods when they contain host-testable logic.
- After making changes, run `make format` when C/C++ files were edited, `make test` when host-testable logic changed, and `make build` when firmware-facing code changed.
- Run the host-side unit tests after changes to pure logic or formatting code, especially `assistant_state.c`, `assistant_command_text.c`, `weather_format.c`, or the unit-test harness itself.
- Run `idf.py build` after changes that affect firmware integration, ESP-IDF-facing code, task orchestration, or component wiring.
