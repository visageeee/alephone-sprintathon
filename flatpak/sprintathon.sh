#!/bin/sh

game_data="$(sprintathon-game-selector)" || exit 0
exec /app/bin/sprintathon-engine "$game_data" "$@"
