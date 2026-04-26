#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/sync-sdkconfig.sh from-main [main-worktree-path]
  scripts/sync-sdkconfig.sh to-main [main-worktree-path]

Copies sdkconfig between the current worktree and the main-branch worktree.

Directions:
  from-main  Copy sdkconfig from the main worktree into this worktree.
  to-main    Copy sdkconfig from this worktree back to the main worktree.

If main-worktree-path is omitted, the script discovers the worktree whose
branch is refs/heads/main using `git worktree list --porcelain`.
EOF
}

find_main_worktree() {
    git worktree list --porcelain | awk '
        /^worktree / {
            worktree = substr($0, 10)
            next
        }
        /^branch refs\/heads\/main$/ {
            print worktree
            found = 1
            exit
        }
        END {
            if (!found) {
                exit 1
            }
        }
    '
}

copy_sdkconfig() {
    local source_path="$1"
    local dest_path="$2"

    if [[ ! -f "$source_path/sdkconfig" ]]; then
        printf 'Missing source sdkconfig: %s\n' "$source_path/sdkconfig" >&2
        exit 1
    fi

    cp "$source_path/sdkconfig" "$dest_path/sdkconfig"
    printf 'Copied %s -> %s\n' "$source_path/sdkconfig" "$dest_path/sdkconfig"
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage >&2
    exit 2
fi

direction="$1"
current_worktree="$(git rev-parse --show-toplevel)"
main_worktree="${2:-}"

if [[ -z "$main_worktree" ]]; then
    if ! main_worktree="$(find_main_worktree)"; then
        printf 'Unable to discover main worktree. Pass it explicitly as the second argument.\n' >&2
        exit 1
    fi
fi

case "$direction" in
    from-main)
        copy_sdkconfig "$main_worktree" "$current_worktree"
        ;;
    to-main)
        copy_sdkconfig "$current_worktree" "$main_worktree"
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
