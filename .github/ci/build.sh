#!/usr/bin/env sh
set -eu
export CCACHE_DIR="${CCACHE_DIR:-$RUNNER_TEMP/ccache}"
ccache --zero-stats
trap 'ccache --show-stats || :' EXIT
./build.sh
