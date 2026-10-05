#!/bin/bash
# Installs the Monomodule plugins (VST3 + AU), the standalone apps and the Library app from this folder.
# Double-click it in Finder (or run: bash "Install Monomodule.command"). Nothing outside your user folder is touched.
#
# The builds are not notarised by Apple, so macOS quarantines them when they are downloaded. This script clears
# that flag and gives every bundle a local (ad-hoc) signature so hosts and Gatekeeper will load them.
set -e
cd "$(dirname "$0")"

VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
APPS="$HOME/Applications"   # the plugins look for the Library app here (or in /Applications)
mkdir -p "$VST3" "$AU" "$APPS"

install_bundle() {   # $1 = bundle, $2 = destination folder
    local name dest
    name="$(basename "$1")"
    dest="$2/$name"
    rm -rf "$dest"
    cp -R "$1" "$dest"
    xattr -dr com.apple.quarantine "$dest" 2>/dev/null || true
    codesign --force --deep --sign - "$dest" >/dev/null 2>&1 || true
    echo "  $dest"
}

echo "Installing Monomodule..."
shopt -s nullglob
for b in VST3/*.vst3;          do install_bundle "$b" "$VST3"; done
for b in AU/*.component;       do install_bundle "$b" "$AU";   done
for b in Standalone/*.app;     do install_bundle "$b" "$APPS"; done
for b in Library/*.app;        do install_bundle "$b" "$APPS"; done

# make Logic / GarageBand / Live rescan the Audio Units
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo
echo "Done. Restart your DAW and rescan plugins."
echo "The first time you open a plugin it asks for the Elektron OS file (.syx):"
echo "  Monomodule One / Six / FX : the Monomachine OS (e.g. Elektron_SFX6-60_OS1.32B.syx)"
echo "  Monomodule MD             : the Machinedrum OS  (e.g. Elektron_SPS1-1UW_OS1.63.syx)"
echo "Both are free downloads from elektron.se (Support > Downloads)."
echo
read -n 1 -s -r -p "Press any key to close."
echo
