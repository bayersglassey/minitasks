#!/bin/bash
set -euo pipefail

# I'm using an Elegoo Nano V3.0, which is a knockoff of an Arduino Nano.
# I found this F_CPU value by seeing what the Arduino IDE used when compiling
# for the Nano.
F_CPU=16000000

BAUD="$(./get_baud.sh)"

avr-gcc \
    --std=c99 \
    -mmcu=atmega328p \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Os \
    -DBAUD="$BAUD"UL \
    -DF_CPU="$F_CPU"UL \
    "$@"
