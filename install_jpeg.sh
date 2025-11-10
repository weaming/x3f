#!/bin/bash
# Script to build libjpeg-turbo for specified target architecture

set -e

TARGET=$1
CMAKE_TOOLCHAIN=$2

if [ -z "$TARGET" ]; then
    echo "Usage: $0 <target> [cmake_toolchain]"
    exit 1
fi

JPEG_VERSION="2.1.5.1"
JPEG_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${JPEG_VERSION}.tar.gz"
JPEG_DIR="libjpeg-turbo-${JPEG_VERSION}"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DEPS_SRC="${SCRIPT_DIR}/deps/src"
BUILD_DIR="${DEPS_SRC}/${TARGET}/jpeg_build"
INSTALL_DIR="${SCRIPT_DIR}/deps/lib/${TARGET}/jpeg"

echo "Building libjpeg-turbo ${JPEG_VERSION} for ${TARGET}"

# Create directories
mkdir -p "${DEPS_SRC}"
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_DIR}"

# Download source if not exists
cd "${DEPS_SRC}"
if [ ! -d "${JPEG_DIR}" ]; then
    echo "Downloading libjpeg-turbo ${JPEG_VERSION}..."
    curl -L "${JPEG_URL}" -o "jpeg-${JPEG_VERSION}.tar.gz"
    tar xzf "jpeg-${JPEG_VERSION}.tar.gz"
    rm "jpeg-${JPEG_VERSION}.tar.gz"
fi

# Configure CMake options based on target
cd "${BUILD_DIR}"

CMAKE_OPTS="-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
CMAKE_OPTS="${CMAKE_OPTS} -DCMAKE_BUILD_TYPE=Release"
CMAKE_OPTS="${CMAKE_OPTS} -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
CMAKE_OPTS="${CMAKE_OPTS} -DENABLE_SHARED=OFF"
CMAKE_OPTS="${CMAKE_OPTS} -DENABLE_STATIC=ON"
CMAKE_OPTS="${CMAKE_OPTS} -DWITH_TURBOJPEG=OFF"

# Extract target system and CPU
TARGET_SYS=$(echo ${TARGET} | sed "s/-.*$//")
TARGET_CPU=$(echo ${TARGET} | sed "s/^${TARGET_SYS}-//")

if [ "$TARGET_SYS" = "osx" ]; then
    if [ "$TARGET_CPU" = "x86_64" ]; then
        CMAKE_OPTS="${CMAKE_OPTS} -DCMAKE_OSX_ARCHITECTURES=x86_64"
    elif [ "$TARGET_CPU" = "arm64" ]; then
        CMAKE_OPTS="${CMAKE_OPTS} -DCMAKE_OSX_ARCHITECTURES=arm64"
    fi
fi

echo "Configuring libjpeg-turbo..."
cmake ${CMAKE_OPTS} "${DEPS_SRC}/${JPEG_DIR}"

echo "Building libjpeg-turbo..."
make -j$(sysctl -n hw.ncpu)

echo "Installing libjpeg-turbo..."
make install

# Create success marker
touch "${INSTALL_DIR}/.success"

echo "libjpeg-turbo built successfully for ${TARGET}"
echo "Install directory: ${INSTALL_DIR}"
