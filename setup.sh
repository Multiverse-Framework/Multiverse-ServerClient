#!/bin/bash

echo "Building multiverse_server_rust..."
(cd multiverse_server_rust || exit; make clean; make install)

export PATH=/usr/bin

echo "Building multiverse_server..."
(cd multiverse_server || exit; make clean; make)

echo "Building multiverse_client..."
(cd multiverse_client || exit; make clean; make)