#!/bin/bash
set -euo pipefail

./stty.sh
cat "$(./get_ttyusb.sh)"
