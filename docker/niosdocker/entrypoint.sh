#!/usr/bin/env bash

# We are entering as root here.
set -euo pipefail

WORKROOT="/__w"
DEFAULT_UID="${DEFAULT_UID:-1000}"
DEFAULT_GID="${DEFAULT_GID:-1000}"
USER_NAME="${RUNNER_USER:-runner}"
GROUP_NAME="${RUNNER_GROUP:-runner}"
HOME_DIR="${RUNNER_HOME:-/home/${USER_NAME}}"

# Determine UID/GID from the workspace mount (prefer /__w itself)
if [ -e "$WORKROOT" ]; then
  WS_UID="$(stat -c '%u' "$WORKROOT")"
  WS_GID="$(stat -c '%g' "$WORKROOT")"
else
  WS_UID="$DEFAULT_UID"
  WS_GID="$DEFAULT_GID"
fi

RUNNER_USER="runner"
RUNNER_HOME="/home/$RUNNER_USER"
RUNNER_GROUP="runner"

groupadd -g $WS_GID $RUNNER_GROUP || true
useradd -m -u $WS_UID -g $WS_GID $RUNNER_USER || true
mkdir -p $RUNNER_HOME || true
chown -R $RUNNER_USER:$RUNNER_GROUP $RUNNER_HOME || true

#Run the rest as user.
exec gosu "${WS_UID}:${WS_GID}" bash -lc '
    set -euo pipefail
    export HOME="'"${RUNNER_HOME}"'"
    git config --global --add safe.directory "*"
    # source $IDF_PATH/export.sh
    cd /__w    
    exec "$@"
' bash "$@"
