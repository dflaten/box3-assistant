#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is not installed or not on PATH" >&2
    exit 1
fi

cd "${ROOT_DIR}"

find main tests -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i
