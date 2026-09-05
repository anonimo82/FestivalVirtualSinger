#!/usr/bin/env sh
set -eu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"${JOBS:-2}"
echo "Built: build/FestivalVirtualSinger"
