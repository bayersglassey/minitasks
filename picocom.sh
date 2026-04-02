
BAUD="$(./get_baud.sh)"

args="-b $BAUD"
if test -z "$NOMAPS"; then args="$args --emap spchex,8bithex --imap spchex,8bithex"; fi
if test -z "$NOECHO"; then args="$args -c"; fi

tty="$(./get_ttyusb.sh)"

exec picocom $args "$@" "$tty"
