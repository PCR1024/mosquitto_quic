#!/bin/bash

# 默认编译模式为本地编译
BUILD_MODE="local"
QL_SDK_DIR=""

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--cross)
            BUILD_MODE="cross"
            shift
            ;;
        --ql-sdk-dir)
            QL_SDK_DIR="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# 检查是否在交叉编译模式下提供了 QL_SDK_DIR
if [ "$BUILD_MODE" == "cross" ] && [ -z "$QL_SDK_DIR" ]; then
    echo "Error: QL_SDK_DIR is required for cross-compilation. Please provide a path using --ql-sdk-dir."
    exit 1
fi

# 清理构建目录
rm -rf build/

# 根据编译模式选择不同的 CMake 参数
if [ "$BUILD_MODE" == "cross" ]; then
    echo "Cross-compiling with QL_SDK_DIR=$QL_SDK_DIR..."
    cmake -DCMAKE_TOOLCHAIN_FILE=./toolchain.cmake -DQL_SDK_DIR=$QL_SDK_DIR -DCMAKE_SYSROOT=$QL_SDK_DIR/ql-sysroots -DCMAKE_FIND_ROOT_PATH=$QL_SDK_DIR/ql-sysroots -B build -S .
else
    echo "Local compiling..."
    cmake -B build -S .
fi

# 构建项目
cmake --build build -j 8