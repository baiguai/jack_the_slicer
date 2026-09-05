#!/bin/bash

# Run Script
# This script builds and runs your application

set -e  # Exit on any error
source ./config.sh

echo "Building $APP_NAME..."
./build.sh

# Check if build was successful
if [ ! -f "build/bin/$APP_NAME" ]; then
    echo "! Build failed ! -- Cannot start $APP_NAME."
    exit 1
fi

echo "-- Build successful --"
echo ""
echo ""
echo "Starting $APP_NAME..."
echo ""
echo "----------------------------------------"
echo ""
echo ""

# Run the application
"./build/bin/$APP_NAME"
