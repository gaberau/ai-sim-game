#!/usr/bin/env bash
set -euo pipefail

# Resolve the project from this script, independent of the caller's directory.
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$project_root"

case "${1:-build}" in
    build)
        for tool in cmake g++ git; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo "Missing WSL build dependency: $tool" >&2
                echo 'Run once in Ubuntu: sudo apt update && sudo apt install build-essential cmake git libncurses-dev' >&2
                exit 1
            fi
        done
        # Separate from older build/ folders to avoid stale absolute CMake paths.
        cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release
        cmake --build build-wsl --parallel 2
        ;;
    run)
        export TERM=xterm-256color
        status=0
        ./build-wsl/ai_sim || status=$?
        if (( status != 0 )); then
            printf '\nAI Sim exited with error %s.\n' "$status"
        fi
        printf '\nPress any key to close this window...'
        read -r -n 1 -s || true
        printf '\n'
        exit "$status"
        ;;
    *)
        echo 'Usage: bash scripts/build-and-run.sh [build|run]' >&2
        exit 1
        ;;
esac
