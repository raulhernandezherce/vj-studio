# source tools/env.sh — pone en PATH las herramientas de .toolchain/
_vj_root="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="$_vj_root/.toolchain/cmake/CMake.app/Contents/bin:$_vj_root/.toolchain/node/bin:$PATH"
