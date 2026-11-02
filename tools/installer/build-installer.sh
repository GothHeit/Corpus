#!/bin/sh
# Builds the installer around an existing Corpus.exe (from build-gui, with libmpv embedded: about 32 MB).
# Usage: tools/installer/build-installer.sh path/to/Corpus.exe [version] [output]
set -e
here=$(cd "$(dirname "$0")" && pwd)
exe=$(realpath "$1")
version=${2:-0.1.0}
case "$version" in
  [0-9]*.[0-9]*.[0-9]*) ;;
  *) echo "version must be N.N.N" >&2; exit 1 ;;
esac
out=${3:-Corpus-Setup.exe}
case "$out" in /*) ;; *) out="$PWD/$out" ;; esac
makensis -V2 -DEXE="$exe" -DVERSION="$version" -DOUTFILE="$out" -DICON="$here/../../apps/gui/corpus.ico" "$here/corpus.nsi"
echo "$out"
