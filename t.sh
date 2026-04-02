#!/bin/bash
#
# Compile & disassemble t.c, so we can see what kind of assembly
# avr-gcc generates for small C snippets.
#
set -euo pipefail

./_compile.sh t.c

avr-objdump -d a.out
