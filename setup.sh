#!/bin/bash

if command -v cargo >/dev/null 2>&1; then
    echo "Building multiverse_server_rust..."
    (cd multiverse_server_rust || exit; make clean; make install)
fi

export PATH=/usr/bin

echo "Building multiverse_server_cpp..."
(cd multiverse_server_cpp || exit; make clean; make)

echo "Building multiverse_client..."
(cd multiverse_client || exit; make clean; make)