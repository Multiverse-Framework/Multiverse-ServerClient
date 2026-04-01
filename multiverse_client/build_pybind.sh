#!/usr/bin/env bash
set -euo pipefail

# Minor versions you actually have installed in the environment
PY_MINORS=(8 9 10 11 12 13 14)

for m in "${PY_MINORS[@]}"; do
  PY="python3.${m}"

  if ! command -v "${PY}" >/dev/null 2>&1; then
    echo "Skip ${PY} (not installed)."
    continue
  fi

  echo "Building multiverse_client pybind for ${PY}..."

  PY_INC="$("${PY}" -c 'import sysconfig; print(sysconfig.get_path("include"))')"
  PYB_INC="$("${PY}" -c 'import pybind11; print(pybind11.get_include())')"

  # Correct extension suffix for this interpreter (includes the ABI tag)
  EXT_SUFFIX="$("${PY}" -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX") or ".so")')"

  # Prefer python-config for link flags (works better across distros)
  # --embed exists on newer pythons; fall back if not supported.
  if "${PY}-config" --help 2>/dev/null | grep -q -- '--embed'; then
    PY_LDFLAGS="$("${PY}-config" --ldflags --embed)"
  else
    PY_LDFLAGS="$("${PY}-config" --ldflags)"
  fi

  # Build output filename that matches Python ABI
  OUT="../lib/linux/multiverse_client_pybind${EXT_SUFFIX}"

  g++ -O3 -Wall -std=c++17 -fPIC -fvisibility=hidden -shared \
    -I./ \
    -I"${PY_INC}" \
    -I"${PYB_INC}" \
    -I./../ext/include \
    -I./../ext \
    -L../lib/linux \
    multiverse_client_pybind.cpp \
    -o "${OUT}" \
    ../lib/linux/libmultiverse_client_all.a \
    -l:libzmq.a \
    -pthread \
    "${PY_LDFLAGS}"

  echo "  -> ${OUT}"
done
