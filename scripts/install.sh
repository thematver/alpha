#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
source_bundle="$root/build/Alpha_artefacts/Release/AU/alpha.component"
components="$HOME/Library/Audio/Plug-Ins/Components"
destination="$components/alpha.component"
if [[ ! -d "$source_bundle" ]]; then
  echo "Build alpha first: bash scripts/build.sh" >&2
  exit 1
fi
if [[ -d "$destination" ]]; then
  existing_id="$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$destination/Contents/Info.plist")"
  if [[ "$existing_id" != "audio.alpha.character" ]]; then
    echo "A different plug-in already occupies $destination; installation stopped." >&2
    exit 1
  fi
fi
/usr/bin/codesign --force --deep --sign - "$source_bundle"
/usr/bin/codesign --verify --deep --strict "$source_bundle"
mkdir -p "$components"
stage="$components/.alpha-install-$$"
trap 'rm -rf "$stage"' EXIT
/usr/bin/ditto "$source_bundle" "$stage"
if [[ -d "$destination" ]]; then
  backup="$root/build/installed-backups/$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$backup"
  mv "$destination" "$backup/alpha.component"
  echo "Previous version preserved: $backup/alpha.component"
fi
mv "$stage" "$destination"
echo "Installed: $destination"
validation_log="$root/build/auval-latest.log"
if /usr/bin/auval -v aufx Alfa Alph -strict > "$validation_log" 2>&1; then
  cat "$validation_log"
elif /usr/bin/grep -q "didn't find the component" "$validation_log"; then
  /usr/bin/killall AudioComponentRegistrar >/dev/null 2>&1 || true
  /usr/bin/auval -v aufx Alfa Alph -strict > "$validation_log" 2>&1 || { cat "$validation_log"; exit 1; }
  cat "$validation_log"
else
  cat "$validation_log"
  exit 1
fi
