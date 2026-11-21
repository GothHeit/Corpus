#!/bin/sh
# Builds a release: Corpus.exe (libmpv inside), Corpus-Setup.exe, SHA256SUMS and SHA256SUMS.sig, all in release/v<version>.
# The version is the one in CMakeLists.txt. Run tools/release-key.sh once first (it makes the signing key).
# Usage: tools/release.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
keydir=${CORPUS_RELEASE_KEY_DIR:-$HOME/.corpus-release-key}
libmpv=${CORPUS_LIBMPV_DLL:-$HOME/projetos/corpus-libmpv-lgpl-out/libmpv-2.dll}

# The private key must never be somewhere git could pick it up.
keyreal=$(realpath -m "$keydir")
case "$keyreal/" in
  "$root"/*) echo "the release key folder $keyreal is inside the repository; move it out" >&2; exit 1 ;;
esac
[ -f "$keydir/private.pem" ] || { echo "no signing key in $keydir; run tools/release-key.sh first" >&2; exit 1; }
[ -f "$libmpv" ] || { echo "libmpv-2.dll not found at $libmpv (set CORPUS_LIBMPV_DLL)" >&2; exit 1; }

version=$(sed -n 's/^project(Corpus VERSION \([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' "$root/CMakeLists.txt")
[ -n "$version" ] || { echo "no version in CMakeLists.txt" >&2; exit 1; }

# The public key built into the program must be this key's, or no installed Corpus could verify the update.
cur=$(sh "$here/release-key.sh" 2>/dev/null >/dev/null; cd "$root" && git status --porcelain apps/gui/update_key.hpp)
[ -z "$cur" ] || { echo "apps/gui/update_key.hpp does not match the signing key; commit the regenerated file first" >&2; exit 1; }

cp "$libmpv" "$root/build-gui/libmpv-2.dll" 2>/dev/null || { mkdir -p "$root/build-gui" && cp "$libmpv" "$root/build-gui/libmpv-2.dll"; }
cmake -S "$root" -B "$root/build-gui" -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-toolchain.cmake" >/dev/null
cmake --build "$root/build-gui" --target corpus-gui -- -j4

out="$root/release/v$version"
rm -rf "$out"
mkdir -p "$out"
cp "$root/build-gui/Corpus.exe" "$out/Corpus.exe"
size=$(wc -c < "$out/Corpus.exe")
if [ "$size" -lt 30000000 ] || [ "$size" -gt 36000000 ]; then
  echo "Corpus.exe is $size bytes; expected about 32-33 MB (is libmpv embedded?)" >&2
  exit 1
fi

sh "$here/installer/build-installer.sh" "$out/Corpus.exe" "$version" "$out/Corpus-Setup.exe" >/dev/null

# The first line names the release; the signature covers it, so the updater can refuse an older signed release.
(cd "$out" && { echo "version $version"; sha256sum Corpus.exe Corpus-Setup.exe; } > SHA256SUMS)

# The signature is the raw r||s (64 bytes) that BCryptVerifySignature takes, not openssl's DER.
openssl dgst -sha256 -sign "$keydir/private.pem" -out "$out/SHA256SUMS.der" "$out/SHA256SUMS"
python3 -I - "$out/SHA256SUMS.der" "$out/SHA256SUMS.sig" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
def read_len(i):
    n = d[i]
    if n < 0x80:
        return n, i + 1
    k = n & 0x7f
    return int.from_bytes(d[i + 1:i + 1 + k], 'big'), i + 1 + k
assert d[0] == 0x30
_, i = read_len(1)
parts = []
for _ in range(2):
    assert d[i] == 0x02
    n, i = read_len(i + 1)
    parts.append(int.from_bytes(d[i:i + n], 'big').to_bytes(32, 'big'))
    i += n
open(sys.argv[2], 'wb').write(parts[0] + parts[1])
PY
rm "$out/SHA256SUMS.der"
[ "$(wc -c < "$out/SHA256SUMS.sig")" -eq 64 ] || { echo "bad signature size" >&2; exit 1; }

echo "Corpus $version: $out"
ls -l "$out"
