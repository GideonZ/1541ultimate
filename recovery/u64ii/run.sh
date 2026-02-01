#!/bin/bash

# get path to script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# check, that venv exists
if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "Fehler: Virtual Environment nicht gefunden. Bitte zuerst ./install.sh ausführen."
    exit 1
fi

# start script from venv 
$SCRIPT_DIR/.venv/bin/python3 $SCRIPT_DIR/recover.py "$@"