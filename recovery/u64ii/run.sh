#!/bin/bash

# Pfad zum Script-Verzeichnis ermitteln
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Prüfen, ob das venv existiert
if [ ! -d "$SCRIPT_DIR/.venv" ]; then
    echo "Fehler: Virtual Environment nicht gefunden. Bitte zuerst ./install.sh ausführen."
    exit 1
fi

# Script mit dem Python-Interpreter aus dem venv starten
# Ersetze 'main.py' durch dein tatsächliches Start-Script
$SCRIPT_DIR/.venv/bin/python3 $SCRIPT_DIR/recover.py "$@"