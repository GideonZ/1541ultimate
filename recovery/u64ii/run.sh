#!/bin/bash

# get path to script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# check, that venv exists
if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "Error: Virtual environment not found. Please execute ./install.sh first."
    exit 1
fi

# start script from venv 
$SCRIPT_DIR/.venv/bin/python3 $SCRIPT_DIR/recover.py "$@"