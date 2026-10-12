#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FRAMEWORK="$ROOT/psxrecomp"
PIN=$(tr -d '[:space:]' < "$ROOT/framework/PIN")

for command in git cmake ctest python3; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Missing prerequisite: $command" >&2
        exit 1
    }
done
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' || {
    echo "Python 3.10 or newer is required." >&2
    exit 1
}
# DISRUPTOR_WITHOUT_GAME=1 is for a machine without the game: every source is
# compiled, the runtime is not linked, and the tests that read no retail data run.
WITHOUT_GAME="${DISRUPTOR_WITHOUT_GAME:-}"
[ -n "$WITHOUT_GAME" ] || [ -f "$ROOT/input/SLUS_002.24" ] || {
    echo "Copy the verified SLUS_002.24 into input/ before building." >&2
    exit 1
}

if [ ! -d "$FRAMEWORK/.git" ]; then
    git clone https://github.com/mstan/psxrecomp.git "$FRAMEWORK"
fi
git -C "$FRAMEWORK" fetch --quiet origin "$PIN"
git -C "$FRAMEWORK" checkout --detach "$PIN"
python3 "$ROOT/tools/framework.py" apply --framework "$FRAMEWORK"

GENERATOR=""
if command -v ninja >/dev/null 2>&1; then GENERATOR="-G Ninja"; fi
# Named in CMake's own environment variables, a launcher also wraps every configure probe, which no cache reuses.
LAUNCHER=""
if [ -n "${DISRUPTOR_COMPILER_LAUNCHER:-}" ]; then
    LAUNCHER="-DCMAKE_C_COMPILER_LAUNCHER=$DISRUPTOR_COMPILER_LAUNCHER -DCMAKE_CXX_COMPILER_LAUNCHER=$DISRUPTOR_COMPILER_LAUNCHER"
fi

# shellcheck disable=SC2086
cmake -S "$FRAMEWORK/recompiler" -B "$FRAMEWORK/recompiler/build" $GENERATOR $LAUNCHER \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$FRAMEWORK/recompiler/build" --parallel \
    --target psxrecomp-game psxrecomp-bios

cd "$ROOT"
if [ -z "$WITHOUT_GAME" ]; then
    python3 tools/inspect_exe.py
    python3 tools/extract_code_image.py
    python3 tools/generate_seeds.py
    "$FRAMEWORK/recompiler/build/psxrecomp-game" --config game.toml
    python3 tools/audit_codegen.py
fi

cd "$FRAMEWORK"
bash tools/regen_bios.sh --config bios/OpenBIOS.toml

cd "$ROOT"
if [ -n "$WITHOUT_GAME" ]; then
    command -v ninja >/dev/null 2>&1 || {
        echo "A build without the game needs Ninja: it names the object files." >&2
        exit 1
    }
    BUILD="${DISRUPTOR_BUILD_DIRECTORY:-build-without-game}"
    DISPATCH="$ROOT/generated/SLUS_002.24.code_dispatch.c"
    if [ ! -e "$DISPATCH" ]; then
        # The framework stops a build whose translated code is absent. An empty
        # file passes that check, and it is neither compiled nor linked here.
        mkdir -p "$ROOT/generated"
        : > "$DISPATCH"
        trap 'rm -f "$DISPATCH"' EXIT
    fi
    # shellcheck disable=SC2086
    cmake -S . -B "$BUILD" -G Ninja $LAUNCHER -DCMAKE_BUILD_TYPE=Release \
        -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF -DBUILD_TESTING=ON
    TARGETS=$(ninja -C "$BUILD" -t targets all)
    OBJECTS=$(printf '%s\n' "$TARGETS" | sed -n 's/^\(.*\.o\(bj\)\{0,1\}\): .*/\1/p' |
        grep -v 'generated/SLUS_')
    TESTS=$(printf '%s\n' "$TARGETS" | sed -n 's/^\([A-Za-z0-9_-]*-test\): .*/\1/p')
    [ -n "$OBJECTS" ] && [ -n "$TESTS" ] || {
        echo "The build names no object files or no tests." >&2
        exit 1
    }
    # The launcher needs nothing of the game, so it is linked where it exists.
    case "$TARGETS" in *"disruptor-launcher: "*) TESTS="$TESTS disruptor-launcher" ;; esac
    # A command line holds about 32 thousand characters on Windows and the object names alone are more: 100 names a call.
    # shellcheck disable=SC2086
    printf '%s\n' $OBJECTS $TESTS | xargs -n 100 ninja -C "$BUILD"
    ctest --test-dir "$BUILD" --output-on-failure --no-tests=error -LE retail
    # The retail contracts still check the sources when the image is not asked for.
    for contract in tests/contracts/test_*_contract.py; do
        python3 -B "$contract"
    done
    echo "Built and tested without the game: the runtime was compiled and not linked."
    exit 0
fi

BUILD="${DISRUPTOR_BUILD_DIRECTORY:-build}"
# shellcheck disable=SC2086
cmake -S . -B "$BUILD" $GENERATOR $LAUNCHER -DCMAKE_BUILD_TYPE=Release \
    -DPSX_RECOMP_UI=OFF -DPSX_ENABLE_VULKAN=OFF
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure

echo "Build complete. Add the matching BIN/CUE, then run ./run.sh."
