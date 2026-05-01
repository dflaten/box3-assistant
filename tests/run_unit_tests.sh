#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/tests/build"

mkdir -p "${BUILD_DIR}"

cc -std=c11 -Wall -Wextra -Werror \
    -I"${ROOT_DIR}/main" \
    -I"${ROOT_DIR}/main/commands" \
    -I"${ROOT_DIR}/main/weather" \
    -I"${ROOT_DIR}/main/hue" \
    -I"${ROOT_DIR}/tests" \
    "${ROOT_DIR}/tests/test_main.c" \
    "${ROOT_DIR}/tests/test_assistant_state.c" \
    "${ROOT_DIR}/tests/test_command_dispatch.c" \
    "${ROOT_DIR}/tests/test_command_text.c" \
    "${ROOT_DIR}/tests/test_hue_discovery_response.c" \
    "${ROOT_DIR}/tests/test_local_stt_protocol.c" \
    "${ROOT_DIR}/tests/test_timer_parse.c" \
    "${ROOT_DIR}/tests/test_timer_runtime.c" \
    "${ROOT_DIR}/tests/test_weather_format.c" \
    "${ROOT_DIR}/main/commands/assistant_command_dispatch.c" \
    "${ROOT_DIR}/main/commands/assistant_command_text.c" \
    "${ROOT_DIR}/main/assistant_state.c" \
    "${ROOT_DIR}/main/hue/hue_command_map.c" \
    "${ROOT_DIR}/main/hue/hue_discovery_response.c" \
    "${ROOT_DIR}/main/stt/local_stt_protocol.c" \
    "${ROOT_DIR}/main/timer/timer_parse.c" \
    "${ROOT_DIR}/main/timer/timer_runtime.c" \
    "${ROOT_DIR}/main/weather/weather_format.c" \
    -o "${BUILD_DIR}/unit_tests"

"${BUILD_DIR}/unit_tests"
