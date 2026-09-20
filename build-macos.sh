#!/usr/bin/env bash
#
# One-command build and install of SPX Ambience on macOS.
#
#   ./build-macos.sh              build, install, ad-hoc sign, validate the AU
#   ./build-macos.sh --no-auval   skip AU validation
#   ./build-macos.sh --no-sign    skip ad-hoc signing
#   ./build-macos.sh --clean      configure from scratch
#
# The plugin installs itself to ~/Library/Audio/Plug-Ins on a successful
# build, so there is nothing to drag anywhere afterwards.

set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
readonly AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
readonly VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"

run_auval=1
do_sign=1
do_clean=0

for arg in "$@"; do
    case "$arg" in
        --no-auval) run_auval=0 ;;
        --no-sign)  do_sign=0 ;;
        --clean)    do_clean=1 ;;
        -h|--help)  sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 1 ;;
    esac
done

say() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
die() { printf '\n\033[31merror:\033[0m %s\n' "$1" >&2; exit 1; }

#------------------------------------------------------------------------------
say "Checking prerequisites"

[[ "$(uname -s)" == "Darwin" ]] || die "this script builds the macOS plugin; on other systems use cmake directly (see README)."

if ! xcode-select -p >/dev/null 2>&1; then
    die "the Xcode command line tools are missing. Install them with:
    xcode-select --install
then run this script again."
fi

if ! command -v cmake >/dev/null 2>&1; then
    die "cmake is missing. Install it with:
    brew install cmake
(or from https://cmake.org/download/), then run this script again."
fi

cmake_version="$(cmake --version | head -1 | awk '{print $3}')"
echo "  Xcode tools : $(xcode-select -p)"
echo "  cmake       : $cmake_version"
echo "  building in : $BUILD_DIR"

#------------------------------------------------------------------------------
if [[ $do_clean -eq 1 && -d "$BUILD_DIR" ]]; then
    say "Removing the previous build directory"
    rm -rf "$BUILD_DIR"
fi

say "Configuring"
# JUCE is downloaded on the first configure, so this step needs the network
# once. Pass JUCE_PATH=/path/to/JUCE to use a checkout you already have.
configure_args=(-S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release)
[[ -n "${JUCE_PATH:-}" ]] && configure_args+=("-DJUCE_PATH=$JUCE_PATH")
cmake "${configure_args[@]}"

say "Building (this takes a few minutes the first time)"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

#------------------------------------------------------------------------------
readonly AU_BUNDLE="$AU_DIR/SPX Ambience.component"
readonly VST3_BUNDLE="$VST3_DIR/SPX Ambience.vst3"

say "Checking what was installed"
installed=0
for bundle in "$AU_BUNDLE" "$VST3_BUNDLE"; do
    if [[ -d "$bundle" ]]; then
        echo "  ok  $bundle"
        installed=$((installed + 1))
    else
        echo "  --  $bundle (not found)"
    fi
done

[[ $installed -gt 0 ]] || die "the build finished but nothing was installed. Check the build output above."

#------------------------------------------------------------------------------
# An ad-hoc signature is enough for local use and keeps hosts that check
# signatures from refusing to load the plugin.
if [[ $do_sign -eq 1 ]]; then
    say "Ad-hoc signing"
    for bundle in "$AU_BUNDLE" "$VST3_BUNDLE"; do
        if [[ -d "$bundle" ]]; then
            codesign --force --sign - --timestamp=none "$bundle" \
                && echo "  signed $(basename "$bundle")" \
                || echo "  could not sign $(basename "$bundle") (it will still load locally)"
        fi
    done
fi

#------------------------------------------------------------------------------
if [[ $run_auval -eq 1 && -d "$AU_BUNDLE" ]]; then
    say "Validating the Audio Unit"
    # Logic and GarageBand cache the plugin list; clearing it makes them
    # re-scan on next launch.
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

    if auval -v aufx Spxa Ccor; then
        echo
        echo "  AU validation passed."
    else
        echo
        echo "  AU validation reported problems. The VST3 is unaffected;" >&2
        echo "  send the output above to have the AU side looked at." >&2
        exit 1
    fi
fi

#------------------------------------------------------------------------------
say "Done"
cat <<EOF
  Audio Unit  ${AU_BUNDLE}
                -> Logic Pro, GarageBand, Live, Reaper
  VST3        ${VST3_BUNDLE}
                -> Ableton Live, Reaper, Studio One, Bitwig, Cubase, FL Studio
  Standalone  ${BUILD_DIR}/SPXAmbience_artefacts/Release/Standalone/SPX Ambience.app

  Rescan plugins in your DAW, then look for "SPX Ambience" under Connor Corwin.
EOF
