#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: scripts/package.sh RELEASE_BUILD OUTPUT_DIR" >&2
    exit 2
fi
build=$(realpath "$1")
mkdir -p "$2"
output=$(realpath "$2")
repo=$(cd "$(dirname "$0")/.." && pwd)
version=$("$build/surco" --version)
version=${version#surco }
[[ $(uname -m) == x86_64 && $(uname -s) == Linux ]]
grep -q '^CMAKE_BUILD_TYPE:STRING=Release$' "$build/CMakeCache.txt"
if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Unexpected Surco version" >&2
    exit 1
fi
base="surco-$version-linux-x86_64"
work=$(mktemp -d "$build/package.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
cmake --install "$build" --prefix "$work/stage/$base" >/dev/null
mkdir "$work/extracted"
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$repo" log -1 --format=%ct)}
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
    -C "$work/stage" -cf - "$base" | gzip -n > "$work/$base.tar.gz"
tar -xzf "$work/$base.tar.gz" -C "$work/extracted"
prefix="$work/extracted/$base"
cli="$prefix/bin/surco"

"$cli" doctor
"$cli" pack "$repo/tests/fixtures/records.csv" -o "$work/samples.surco"
"$cli" verify "$work/samples.surco"
"$cli" unpack "$work/samples.surco" -o "$work/restored.csv"
cmp "$work/restored.csv" "$repo/tests/fixtures/records.csv"
example="$prefix/share/surco/examples/c/read.c"
"${CC:-cc}" -std=c17 -Wall -Wextra -Werror -I"$prefix/include" "$example" \
    -L"$prefix/lib" -lsurco -o "$work/read-shared"
"${CC:-cc}" -std=c17 -Wall -Wextra -Werror -I"$prefix/include" "$example" \
    "$prefix/lib/libsurco.a" -o "$work/read-static"
tail -n +2 "$work/restored.csv" > "$work/expected.csv"
LD_LIBRARY_PATH="$prefix/lib" "$work/read-shared" "$work/samples.surco" | cmp - "$work/expected.csv"
"$work/read-static" "$work/samples.surco" | cmp - "$work/expected.csv"
for elf in "$cli" "$prefix/lib/libsurco.so"; do
    readelf -h "$elf" | awk '/Machine:.*X86-64/ {found=1} END {exit !found}'
    readelf -W -l "$elf" | awk '$1 == "GNU_STACK" { seen=1; if ($7 ~ /E/ || $8 == "E") bad=1 } END { exit !seen || bad }'
    printf '%s libc floor ' "$(basename "$elf")"
    readelf -W --version-info "$elf" | awk '/Name: GLIBC_/ {print $3}' | sort -V | tail -1
done
readelf -W --dyn-syms "$prefix/lib/libsurco.so" | awk '/surco_decoder_read/ {found=1} END {exit !found}'
(
    cd "$work"
    sha256sum "$base.tar.gz" > SHA256SUMS
    sha256sum -c SHA256SUMS
)
install -m 644 "$work/$base.tar.gz" "$work/SHA256SUMS" "$output/"
