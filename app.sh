#!/bin/bash

# Run Script
# This script JUST runs your application

set -e  # Exit on any error
source ./config.sh

echo "...Starting $APP_NAME..."

# Check if build was successful
if [ ! -f "build/bin/$APP_NAME" ]; then
    echo "! Run failed ! -- Cannot start $APP_NAME."
    exit 1
fi

# Run the application
"./build/bin/$APP_NAME"
