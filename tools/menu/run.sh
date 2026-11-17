#!/bin/bash
# The console F1 menu's checks on the PC, hidden (docs/CONSOLE_MENU.md, "Checks"):
#   run.sh script EXE OUTDIR NAME SCRIPT [ENV=VALUE...]  one menu script run: OUTDIR/NAME.log, its shots in OUTDIR/NAME/
#   run.sh d EXE OUTDIR [d1 ... d8]                       the D scripts (scripts/), three at a time
#   run.sh picture EXE OUTDIR NAME FRAME [ENV=VALUE...]   one picture at FRAME (C): OUTDIR/NAME.bmp + NAME.log
# DW4_DATA (required): the game data, the ISO or an extracted folder. Every run: the console style with Android's
# rules at the Thor's size (231 %, 1920x1080, the Thor's prompts), F1 open, its own fresh card folder, the new game
# of input/newgame_dng101b1.txt (INPUT= another one) as the stage input; at low priority, no error boxes. A run
# ends with its script (exit 3: an expect failed). The pictures stay BMP (game pictures: keep them out of git).
here=$(cd "$(dirname "$0")" && pwd)
[ -n "$DW4_DATA" ] || { echo "run.sh: set DW4_DATA to the game data" >&2; exit 2; }
win() { if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
run() { # EXE OUTDIR NAME FRAMES EXTRA_ARGS ENV...
  local exe=$1 out=$2 name=$3 frames=$4 extra=$5; shift 5
  local card="$out/cards_$name"
  rm -rf "$card"; mkdir -p "$card/Card A" "$card/Card B" "$out/$name"
  ( cd "$out" && env DW4_NO_ERROR_BOX=1 DW4_MENU_STYLE=console DW4_MENU_PLATFORM=android DW4_UI_SCALE=231 DW4_WINDOW=1920x1080 \
    DW4_PROMPTS=thor DW4_OPEN_MENU=1 "$@" nice -n 19 "$exe" --data "$DW4_DATA" --smoke-test "$frames" --card-dir "$(win "$card")" \
    --input "$(win "${INPUT:-$here/input/newgame_dng101b1.txt}")" $extra > "$out/$name.log" 2>&1 )
  local code=$?
  rm -rf "$card"
  return $code
}
mode=$1; shift
case $mode in
script)
  exe=$1 out=$(mkdir -p "$2" && cd "$2" && pwd) name=$3 script=$4; shift 4
  run "$exe" "$out" "$name" "${FRAMES:-30000}" "--menu-script $(win "$script") --screenshot $(win "$out/$name/shot.bmp")" "$@"
  code=$?
  echo "$name: exit $code; $(grep -a 'menu script: done' "$out/$name.log" | tail -1)"
  grep -a 'menu script: FAILED' "$out/$name.log" | head -20
  exit $code ;;
picture)
  exe=$1 out=$(mkdir -p "$2" && cd "$2" && pwd) name=$3 frame=$4; shift 4
  run "$exe" "$out" "$name" "$frame" "--screenshot $(win "$out/$name.bmp") --screenshot-frames $frame" "$@"
  echo "$name: exit $?" ;;
d)
  exe=$1 out=$2; shift 2
  S=$here/scripts
  one() {
    case $1 in
      d1) "$0" script "$exe" "$out" d1 "$S/d1_nav.txt" ;;
      d2) "$0" script "$exe" "$out" d2 "$S/d2_keys.txt" ;;
      d3) "$0" script "$exe" "$out" d3 "$S/d3_states_text.txt" ;;
      d4) "$0" script "$exe" "$out" d4 "$S/d4_touch.txt" DW4_MENU_INPUT=touch DW4_MENU_PAGE=display ;;
      d5) "$0" script "$exe" "$out" d5 "$S/d5_touch_bind.txt" DW4_MENU_INPUT=touch DW4_MENU_PAGE=controls/bindings.buttons ;;
      d6) "$0" script "$exe" "$out" d6 "$S/d6_lower.txt" DW4_MENU_LOWER=1 DW4_WINDOW=1240x1080 DW4_MENU_INPUT=touch DW4_MENU_PAGE=display ;;
      d7) INPUT=$here/input/newgame_soft_reset.txt "$0" script "$exe" "$out" d7 "$S/d7_fixes.txt" DW4_MENU_LAYOUT_LOG=1 ;;
      d8) "$0" script "$exe" "$out" d8 "$S/d8_touch_fixes.txt" DW4_MENU_LAYOUT_LOG=1 DW4_MENU_TRACE=1 DW4_MENU_PAGE=general ;;
    esac
  }
  n=0
  for name in ${*:-d1 d2 d3 d4 d5 d6 d7 d8}; do
    one "$name" &
    n=$((n + 1)); [ $((n % 3)) -eq 0 ] && wait
  done
  wait ;;
*) sed -n '2,9p' "$0"; exit 2 ;;
esac
