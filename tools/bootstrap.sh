#!/usr/bin/env bash
# Instala CMake y Node dentro de .toolchain/ (sin administrador) y las dependencias npm.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE_VER=4.4.5
NODE_VER=v26.13.0
TC="$ROOT/.toolchain"
mkdir -p "$TC"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) NODE_PKG="node-$NODE_VER-darwin-arm64" ;;
  Darwin-x86_64) NODE_PKG="node-$NODE_VER-darwin-x64" ;;
  *) echo "bootstrap.sh solo cubre macOS; en Windows instala CMake y Node a mano." >&2; exit 1 ;;
esac
if [ ! -x "$TC/cmake/CMake.app/Contents/bin/cmake" ]; then
  curl -fsSL "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-macos-universal.tar.gz" -o "$TC/cmake.tgz"
  mkdir -p "$TC/cmake" && tar -xzf "$TC/cmake.tgz" -C "$TC/cmake" --strip-components=1 && rm "$TC/cmake.tgz"
fi
if [ ! -x "$TC/node/bin/node" ]; then
  curl -fsSL "https://nodejs.org/dist/$NODE_VER/$NODE_PKG.tar.gz" -o "$TC/node.tgz"
  mkdir -p "$TC/node" && tar -xzf "$TC/node.tgz" -C "$TC/node" --strip-components=1 && rm "$TC/node.tgz"
fi
export PATH="$TC/cmake/CMake.app/Contents/bin:$TC/node/bin:$PATH"
cmake --version | head -1
echo "node $(node --version)"
