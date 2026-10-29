#!/bin/bash
# Two hidden game clients (a DW4_ONLINE_DEV build) in one room on the loopback
# stub (dw4online_stub.py): pinned HTTPS login and room create / join, the
# DW4R2 relay, solo-led play with LOG, stage reports, PRESENCE between the
# two and their remote name tags. Both play a new game with the campaign bot
# from the title.
#
#   e2e_two_clients.sh EXE DATA OUT [FRAMES] [HTTP_PORT] [RELAY_PORT]
#
# Ports default to 53210 / 53211 (checked with netstat first); OUT receives
# stub.log, a.log, b.log. Only the processes started here are stopped.
export PATH=/usr/bin:/mingw64/bin:/c/Windows/System32:/c/Windows:$PATH
PY=${PY:-/c/Users/shinz/AppData/Local/Programs/Python/Python313/python.exe}
exe=$1 data=$2 out=$3 frames=${4:-7200} http=${5:-53210} relay=${6:-53211}
here=$(cd "$(dirname "$0")" && pwd)
[ -f "$exe" ] && [ -n "$data" ] && [ -n "$out" ] || { sed -n '2,12p' "$0"; exit 2; }
for p in $http $relay; do
  if netstat -ano | grep -q ":$p "; then echo "port $p is in use"; exit 3; fi
done
mkdir -p "$out"; rm -f "$out"/*.log "$out/pin.txt"
printf '0 SEEK 200000 campaign\n' > "$out/campaign.txt"
seconds=$(( frames / 60 + 60 ))
PYTHONIOENCODING=utf-8 "$PY" -I "$here/dw4online_stub.py" $http $relay $seconds "$(cygpath -w "$out/pin.txt")" > "$out/stub.log" 2>&1 &
stub=$!
for i in $(seq 1 100); do [ -s "$out/pin.txt" ] && break; sleep 0.2; done
[ -s "$out/pin.txt" ] || { echo "the stub did not start"; cat "$out/stub.log"; kill $stub; exit 4; }
pin=$(cat "$out/pin.txt")
run() { # NAME ROOM_ARGS...
  local name=$1; shift
  rm -rf "$out/card_$name"; mkdir -p "$out/card_$name/Card A" "$out/card_$name/Card B"
  env DW4_NO_ERROR_BOX=1 DW4_ONLINE_SERVER=127.0.0.1:$http DW4_ONLINE_PIN="$pin" DW4_ONLINE_PASSWORD=stub-only \
      DW4_LABELS=all DW4_TRACE_LABELS=600 DW4_TRACE_FINGERPRINT=600 \
      "$exe" --data "$data" --smoke-test $frames --card-dir "$(cygpath -w "$out/card_$name")" \
      --input "$(cygpath -w "$out/campaign.txt")" --online-login "$name" "$@" > "$out/$name.log" 2>&1
}
run kim --online-create "Stub room" --online-camera 1 &
a=$!
sleep 8 # the room exists before the second client joins
run lee --online-join R001 &
b=$!
wait $a; wait $b
wait $stub
echo "== stub"; cat "$out/stub.log"
for n in kim lee; do
  echo "== $n"; grep -a -E "^online:|stage change|label slot=-1|\] label slot=-1" "$out/$n.log" | head -40
done
