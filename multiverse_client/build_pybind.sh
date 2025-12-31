#!/bin/bash

for PYTHON_MINOR_VERSION in 8 9 10 11 12; do
    PYTHON_VERSION="3.${PYTHON_MINOR_VERSION}"
    echo "Building multiverse_client pybind for Python ${PYTHON_VERSION}..."
    g++ -O3 -Wall -I./ -I/usr/include/python${PYTHON_VERSION} -I/home/giangnguyen/.local/lib/python${PYTHON_VERSION}/site-packages/pybind11/include -I./../ext/include -std=c++17 -fPIC -fvisibility=hidden -shared -L../lib/linux multiverse_client_pybind.cpp -o ../lib/linux/multiverse_client_pybind.cpython-3${PYTHON_MINOR_VERSION}-x86_64-linux-gnu.so ../lib/linux/libmultiverse_client.a -l:libzmq.a /usr/lib/x86_64-linux-gnu/libpython${PYTHON_VERSION}.so -pthread
done