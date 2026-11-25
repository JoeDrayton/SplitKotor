#!/usr/bin/env bash
# UNG-127: install prints the exact Steam launch option and never touches Steam's own files.
. "$(dirname "$0")/_kp_lib.sh"
g="$(mkgame "$RPM_ROOT")"; vdf "$RPM_ROOT" "$RPM_ROOT"
mkdir -p "$RPM_ROOT/userdata/1234/config"; printf '"UserLocalConfigStore" {}\n' > "$RPM_ROOT/userdata/1234/config/localconfig.vdf"
printf 'DLL' > "$TMP/d.dll"
snap() { (cd "$RPM_ROOT" && find . -type f -not -path './steamapps/common/swkotor/*' -print0 | sort -z | xargs -0 sha256sum); }
steam_before="$(snap)"
kp install "$TMP/d.dll"; [ $RC -eq 0 ] || fail "install: rc $RC ($ERR)"
printf '%s\n' "$OUT" | grep -qxF 'WINEDLLOVERRIDES="dinput8=n,b" %command%' || fail "launch option line not found exactly: '$OUT'"
[ "$steam_before" = "$(snap)" ] || fail "a Steam file outside the game folder changed"
pass
