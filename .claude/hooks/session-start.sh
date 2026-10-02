#!/bin/bash
# Cloud sessions (claude.ai/code, Routines) start from a stock Ubuntu image that
# lacks JUCE's Linux headers, so nothing builds until these are installed. Only
# missing packages are installed, so this is a no-op once they are present.
set -euo pipefail

[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0

# Same list as the Linux job in .github/workflows/ci.yml; keep the two in step.
pkgs=(libasound2-dev libjack-jackd2-dev libx11-dev libxcomposite-dev
      libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
      libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev libcurl4-openssl-dev)

# Match Provides too: on 24.04 libfreetype6-dev is a virtual package that
# libfreetype-dev provides, so a plain name check never sees it as installed.
installed=$(dpkg-query -W -f='${db:Status-Abbrev} ${Package} ${Provides}\n' 2>/dev/null | awk '$1 == "ii"')
missing=()
for p in "${pkgs[@]}"; do
    grep -qE "(^| )$p( |,|$)" <<<"$installed" || missing+=("$p")
done
[ ${#missing[@]} -eq 0 ] && exit 0

sudo=""
[ "$(id -u)" -eq 0 ] || sudo="sudo"
# Third-party PPAs in the image can 403 behind the proxy; the Ubuntu archive is all this needs.
$sudo apt-get update -qq >/dev/null 2>&1 || true
$sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${missing[@]}" >/dev/null
echo "Installed JUCE build deps: ${missing[*]}"
