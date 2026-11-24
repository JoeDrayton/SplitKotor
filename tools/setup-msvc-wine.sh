#!/usr/bin/env bash
# Installs msvc-wine (MSVC 2022 x86 and the Windows SDK) into ${MSVC_WINE_DIR:-$HOME/opt/msvc}.
# A second run with everything present changes nothing.
set -euo pipefail

dest="${MSVC_WINE_DIR:-$HOME/opt/msvc}"
# Pinned so every machine gets the same wrappers.
msvc_wine_commit=5dd115047b7ddcbb53cbc8b5b14163a20f4334fe
# Visual Studio 2022; vsdownload.py defaults to a newer major.
vs_major=17
# Written last, so an interrupted install is never taken for a complete one.
marker="$dest/.setup-complete"

has_file() { # has_file <dir> <path pattern>
    [ -d "$1" ] && [ -n "$(find "$1" -ipath "$2" -print -quit)" ]
}
toolchain_present() {
    [ -x "$dest/bin/x86/cl" ] \
        && has_file "$dest/VC/Tools/MSVC" '*/bin/Host*/x86/cl.exe' \
        && has_file "$dest/Windows Kits/10/Include" '*/um/windows.h' \
        && has_file "$dest/Windows Kits/10/Lib" '*/um/x86/user32.lib'
}

if [ -e "$marker" ] && toolchain_present; then
    echo "msvc-wine is already installed in $dest"
    exit 0
fi

for tool in wine python3 git msiextract unzip; do
    command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
git clone --quiet https://github.com/mstorsjo/msvc-wine.git "$work/msvc-wine"
git -C "$work/msvc-wine" checkout --quiet "$msvc_wine_commit"

mkdir -p "$dest"
rm -f "$marker"
# --accept-license accepts Microsoft's Build Tools and Windows SDK licence terms for whoever runs this.
python3 "$work/msvc-wine/vsdownload.py" --accept-license --major "$vs_major" --architecture x86 --dest "$dest"
"$work/msvc-wine/install.sh" "$dest"
toolchain_present || { echo "the install in $dest is incomplete" >&2; exit 1; }
touch "$marker"
echo "msvc-wine installed in $dest"
