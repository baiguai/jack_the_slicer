#!/bin/bash

source ./config.sh

valgrind --leak-check=full "./build/bin/$APP_NAME"
