#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/tests/build"

mkdir -p "${BUILD_DIR}"

cc -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/main" \
    -I"${ROOT_DIR}/main/weather" \
    -I"${ROOT_DIR}/main/hue" \
    "${ROOT_DIR}/tests/unit_tests.c" \
    "${ROOT_DIR}/main/assistant_command_text.c" \
    "${ROOT_DIR}/main/assistant_state.c" \
    "${ROOT_DIR}/main/hue/hue_command_map.c" \
    "${ROOT_DIR}/main/weather/weather_format.c" \
    -o "${BUILD_DIR}/unit_tests"

"${BUILD_DIR}/unit_tests"
