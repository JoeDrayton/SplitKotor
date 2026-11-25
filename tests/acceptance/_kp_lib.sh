# Shared helpers for the UNG-127 acceptance tests (kotor-proton.sh). Sourced; defines functions only.
# Every test runs the real script with HOME pointed at a temp Steam tree, so no real Steam folder is touched.
[ "${BASH_SOURCE[0]}" = "$0" ] && exit 0   # run by the acceptance loop: nothing to do
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
NAME="$(basename "$0" .sh)"
PIDS=()
cleanup() { for p in "${PIDS[@]:-}"; do [ -n "$p" ] && kill "$p" 2>/dev/null; done; [ -n "${TMP:-}" ] && rm -rf "${TMP:?}"; }
trap cleanup EXIT

fail() { echo "FAIL [$NAME]: $*" >&2; exit 1; }
pass() { echo "PASS [$NAME]"; exit 0; }

# The script under test sits in a copy of the repo layout, so QA controls out/dinput8.dll and KotorPlus.ini.
REPO="$TMP/repo"
mkdir -p "$REPO/tools" "$REPO/out"
[ -f "$ROOT/tools/kotor-proton.sh" ] || fail "tools/kotor-proton.sh missing from the tree"
cp "$ROOT/tools/kotor-proton.sh" "$REPO/tools/"
printf 'DEFAULT-DLL-BYTES\n' > "$REPO/out/dinput8.dll"
printf '[KotorPlus]\nFpsCap=240\n' > "$REPO/KotorPlus.ini"
KP="$REPO/tools/kotor-proton.sh"

HOME_T="$TMP/home"; mkdir -p "$HOME_T"
RPM_ROOT="$HOME_T/.local/share/Steam"
FLAT_ROOT="$HOME_T/.var/app/com.valvesoftware.Steam/.local/share/Steam"

# Refuse to run while a real game is up: the script's process check is host-wide.
if pgrep -x swkotor.exe >/dev/null; then fail "a swkotor.exe is already running on this host; run under the swkotor flock with the game closed"; fi

# reset_home: an empty HOME again.  drop_rpm / drop_flatpak: remove one Steam root.
reset_home() { find "${HOME_T:?}" -mindepth 1 -delete; }
drop_rpm() { find "${HOME_T:?}/.local" -mindepth 0 -delete 2>/dev/null; }
drop_flatpak() { find "${HOME_T:?}/.var" -mindepth 0 -delete 2>/dev/null; }
# clean_game <game dir>: remove every file our install could have left, the foreign DLL included.
clean_game() { find "${1:?}" -maxdepth 1 -type f \( -name 'dinput8.dll*' -o -name '*.kotorplus' -o -name 'KotorPlus.*' \) -delete; }

# mkgame <steam root or library root> [exe content]: a Steam library holding app 32370.
mkgame() {
  local lib="$1" exe="${2:-FAKE-EXE}" d="$1/steamapps/common/swkotor"
  mkdir -p "$d"
  printf '"AppState"\n{\n\t"appid"\t\t"32370"\n\t"installdir"\t\t"swkotor"\n}\n' > "$lib/steamapps/appmanifest_32370.acf"
  printf '%s' "$exe" > "$d/swkotor.exe"
  echo "$d"
}

# vdf <steam root> <library path>...: a libraryfolders.vdf naming each path.
vdf() {
  local root="$1"; shift; mkdir -p "$root/steamapps"
  { printf '"libraryfolders"\n{\n'; local i=0 p
    for p in "$@"; do printf '\t"%d"\n\t{\n\t\t"path"\t\t"%s"\n\t\t"label"\t\t""\n\t}\n' "$i" "$p"; i=$((i+1)); done
    printf '}\n'; } > "$root/steamapps/libraryfolders.vdf"
}

# kp <args>: run the script with HOME at the temp tree. Sets OUT (stdout), ERR (stderr), RC.
kp() {
  OUT="$(HOME="$HOME_T" XDG_DATA_HOME= "$KP" "$@" 2>"$TMP/stderr")"; RC=$?
  ERR="$(cat "$TMP/stderr")"
}

# A running process whose name is swkotor.exe, the way Proton shows the game.
start_fake_game() {
  mkdir -p "$TMP/proc"; cp "$(command -v sleep)" "$TMP/proc/swkotor.exe"
  "$TMP/proc/swkotor.exe" 600 & GAME_PID=$!; PIDS+=("$GAME_PID")
  local i; for i in $(seq 50); do pgrep -x swkotor.exe >/dev/null && return 0; sleep 0.1; done
  fail "stand-in swkotor.exe process never appeared"
}
stop_fake_game() { kill "$GAME_PID" 2>/dev/null; wait "$GAME_PID" 2>/dev/null; local i; for i in $(seq 50); do pgrep -x swkotor.exe >/dev/null || return 0; sleep 0.1; done; fail "stand-in swkotor.exe did not stop"; }

# tree_hash <dir>: every file's path and content hash, to prove a folder is byte-identical.
tree_hash() { (cd "$1" && find . -type f -print0 | sort -z | xargs -0 sha256sum); }
