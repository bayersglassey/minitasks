#!/bin/bash
set -euo pipefail

./_compile.sh \
    "$@" \
    -o minitasks.elf \
    tasklang.S minitasks.c

avr-objcopy -j .text -j .data -O ihex minitasks.elf minitasks.hex
