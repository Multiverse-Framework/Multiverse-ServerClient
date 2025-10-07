#!/bin/bash

# Kill the multiverse_server process
pkill -f multiverse_server

# Kill the app_client process
pkill -f test_multiverse_client

echo "Processes killed successfully."
