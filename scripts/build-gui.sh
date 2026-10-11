#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$project_root"
build_directory="${AI_SIM_GUI_BUILD_DIR:-build-gui}"
cmake -S . -B "$build_directory" -DAI_SIM_BUILD_GUI=ON -DAI_SIM_BUILD_TERMINAL=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE="${AI_SIM_BUILD_TYPE:-Release}"
cmake --build "$build_directory" --target ai_sim_gui ai_sim_gui_tests --parallel "${AI_SIM_BUILD_JOBS:-2}"
case "${1:-run}" in
    build) ;;
    test)
        ctest --test-dir "$build_directory" -R graphical_session_story_and_persistence --output-on-failure
        smoke_saves="$(mktemp -d)"
        trap 'rm -rf -- "$smoke_saves"' EXIT
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$build_directory/ai_sim_gui" --new --save-directory "$smoke_saves" --smoke-test
        ;;
    run) "$build_directory/ai_sim_gui" "${@:2}" ;;
    *) echo 'Usage: bash scripts/build-gui.sh [build|test|run] [game options]' >&2; exit 1 ;;
esac
