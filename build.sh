#!/bin/bash
rm -rf build/
cmake -DCMAKE_TOOLCHAIN_FILE=./toolchain.cmake -B build -S .
cmake --build build -j 16