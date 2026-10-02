#!/bin/bash
# servers.sh start|stop <logdir>: the local test servers (OpenSSL >= 3.0 and python3). Ports:
#   8443 TLS 1.3, CA-issued leaf for tls.test      8444 the same, TLS 1.2 only
#   8445 TLS 1.3, leaf for other.test (wrong name)  8446 TLS 1.3, self-signed leaf for tls.test
#   8447 TLS 1.3, expired leaf                      8448 TLS 1.2 without the extended master secret
#   8449 TLS 1.3 (tools/app_server.py): /keepalive (records the client's close_notify, status on TCP 8450), /echo
#   UDP 123 (NTP_PORT) SNTP from this machine's clock
# -WWW serves blob.bin (tools/mkblob.py: 256 KiB, SHA-256 in certs/blob.h). PIDs go to <logdir>/pids.txt.
set -u
CMD=$1
LOG=$2
C=$(cd "$(dirname "$0")/../certs" && pwd)
T=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$LOG"
if [ "$CMD" = stop ]; then
    [ -f "$LOG/pids.txt" ] && for pid in $(awk '{print $1}' "$LOG/pids.txt"); do kill "$pid" 2>/dev/null; done
    rm -rf "$LOG/www"
    exit 0
fi
WWW="$LOG/www"
mkdir -p "$WWW"
python3 "$T/mkblob.py" "$WWW/blob.bin" > "$LOG/blob.sha256"
: > "$LOG/pids.txt"
srv() { # port cert key options...
    local port=$1 cert=$2 key=$3; shift 3
    (cd "$WWW" && exec openssl s_server -accept "$port" -cert "$C/$cert" -key "$C/$key" -WWW -http_server_binmode "$@" \
        > "$LOG/server-$port.log" 2>&1) &
    echo "$! openssl s_server -accept $port $cert $*" >> "$LOG/pids.txt"
}
srv 8443 good.pem good.key.pem -tls1_3
srv 8444 good.pem good.key.pem -tls1_2
srv 8445 wronghost.pem wronghost.key.pem -tls1_3
srv 8446 selfsigned.pem selfsigned.key.pem -tls1_3
srv 8447 expired.pem expired.key.pem -tls1_3
python3 "$T/noems_server.py" 8448 "$C/good.pem" "$C/good.key.pem" > "$LOG/server-8448.log" 2>&1 &
echo "$! noems_server.py 8448" >> "$LOG/pids.txt"
python3 "$T/app_server.py" 8449 8450 "$C/good.pem" "$C/good.key.pem" > "$LOG/server-8449.log" 2>&1 &
echo "$! app_server.py 8449 8450" >> "$LOG/pids.txt"
python3 "$T/sntp_server.py" "${NTP_PORT:-123}" > "$LOG/server-ntp.log" 2>&1 &
echo "$! sntp_server.py ${NTP_PORT:-123}" >> "$LOG/pids.txt"
sleep 2
# Controls: what each server really negotiates, seen by OpenSSL's own client
: > "$LOG/controls.txt"
for port in 8443 8444 8445 8446 8447 8448; do
    echo "== s_client 127.0.0.1:$port -verify_hostname tls.test" >> "$LOG/controls.txt"
    openssl s_client -connect 127.0.0.1:$port -servername tls.test -CAfile "$C/ca.pem" -verify_hostname tls.test \
        < /dev/null 2>&1 | grep -E "^Protocol|Extended master|Verify return" | sort -u >> "$LOG/controls.txt"
done
