#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cmake_bin="${ALPHA_CMAKE:-/opt/homebrew/bin/cmake}"
if [[ ! -d "$root/third_party/JUCE/modules" ]]; then
  mkdir -p "$root/third_party"
  git clone --depth 1 --branch 8.0.15 https://github.com/juce-framework/JUCE.git "$root/third_party/JUCE"
fi
"$cmake_bin" -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
"$cmake_bin" --build "$root/build" --config Release -j 4
cd "$root"
"$cmake_bin" --build "$root/build" --target test
