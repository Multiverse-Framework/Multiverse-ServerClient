#!/bin/bash

export PATH=/usr/bin

echo "Building multiverse_server..."
(cd multiverse_server; make clean; make)

echo "Building multiverse_client..."
(cd multiverse_client; make clean; make)