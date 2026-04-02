#!/bin/bash
set -euo pipefail

BAUD="$(./get_baud.sh)"

avr-gcc \
    --std=c99 \
    -mmcu=atmega328p \
    -DBAUD="$BAUD" \
    "$@" \
    -E -dM \
    tasklang.S minitasks.c \
    >defines.txt
