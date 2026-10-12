#!/usr/bin/env bash
set -euo pipefail

# Resolve the project from this script, independent of the caller's directory.
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd -- "$project_root"

# Keep the Windows launcher default while allowing isolated Linux/CI builds.
build_directory="${AI_SIM_BUILD_DIR:-build-wsl}"

case "${1:-build}" in
    build|test)
        for tool in cmake g++ git; do
            if ! command -v "$tool" >/dev/null 2>&1; then
                echo "Missing WSL build dependency: $tool" >&2
                echo 'Run once in Ubuntu: sudo apt update && sudo apt install build-essential cmake git libncurses-dev' >&2
                exit 1
            fi
        done
        # Separate from older build/ folders to avoid stale absolute CMake paths.
        cmake -S . -B "$build_directory" -DCMAKE_BUILD_TYPE="${AI_SIM_BUILD_TYPE:-Release}" -DBUILD_TESTING=ON
        cmake --build "$build_directory" --parallel "${AI_SIM_BUILD_JOBS:-2}"
        if [[ "${1:-build}" == test ]]; then
            ctest --test-dir "$build_directory" --output-on-failure
            python3 "Development Tools/tests/DrawioTests.py"
            python3 "Development Tools/tests/TerminalSmokeTests.py" "$build_directory/ai_sim"
        fi
        ;;
    run)
        export TERM=xterm-256color
        status=0
        "$build_directory/ai_sim" || status=$?
        if (( status != 0 )); then
            printf '\nAI Sim exited with error %s.\n' "$status"
        fi
        printf '\nPress any key to close this window...'
        read -r -n 1 -s || true
        printf '\n'
        exit "$status"
        ;;
    *)
        echo 'Usage: bash scripts/build-and-run.sh [build|test|run]' >&2
        exit 1
        ;;
esac
