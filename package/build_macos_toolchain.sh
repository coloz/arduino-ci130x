#!/usr/bin/env bash

set -euo pipefail

toolchain_version=9.2.0
source_ref=nuclei_9.2_fixjalr_forhw
source_commit=b709d98b514136ab73118998518caa09aa9ddf22
gcc_commit=b2354399bb7175a7cefb86ed0ba870584ec0324f
binutils_commit=dbfcea998ba6f592566eda9f9288690d7a060c8f
newlib_commit=b8d32e85025f863db1df73ce625c6fddeadb7c17
work_root="${1:-$HOME/ci130x-nuclei-gcc-${toolchain_version}-build}"
output_root="${2:-$PWD/dist}"
source_root="$work_root/source"
build_root="$work_root/build"
prefix="$work_root/riscv-gcc"
resume="${CI130X_TOOLCHAIN_RESUME:-0}"

if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
  echo 'This script requires an Apple Silicon macOS host.' >&2
  exit 1
fi
if [[ "$resume" != 0 && "$resume" != 1 ]]; then
  echo 'CI130X_TOOLCHAIN_RESUME must be 0 or 1.' >&2
  exit 1
fi
if [[ -e "$work_root" && "$resume" != 1 ]]; then
  echo "Build directory already exists; remove it explicitly before retrying: $work_root" >&2
  exit 1
fi
if [[ "$resume" == 1 && ! -d "$source_root" ]]; then
  echo "Cannot resume because the source directory is missing: $source_root" >&2
  exit 1
fi

brew=/opt/homebrew/bin/brew
if [[ ! -x "$brew" ]]; then
  echo 'Homebrew was not found at /opt/homebrew/bin/brew.' >&2
  exit 1
fi

for formula in gawk gnu-sed gmp mpfr libmpc isl zlib expat texinfo gcc@14; do
  "$brew" list --versions "$formula" >/dev/null
done

export PATH="$("$brew" --prefix gnu-sed)/libexec/gnubin:$("$brew" --prefix texinfo)/bin:/opt/homebrew/bin:/opt/homebrew/sbin:$PATH"
export CC="$("$brew" --prefix gcc@14)/bin/gcc-14"
export CXX="$("$brew" --prefix gcc@14)/bin/g++-14"
export CPPFLAGS="-I$("$brew" --prefix gmp)/include -I$("$brew" --prefix mpfr)/include -I$("$brew" --prefix libmpc)/include -I$("$brew" --prefix isl)/include -I$("$brew" --prefix zlib)/include -I$("$brew" --prefix expat)/include"
export LDFLAGS="-L$("$brew" --prefix gmp)/lib -L$("$brew" --prefix mpfr)/lib -L$("$brew" --prefix libmpc)/lib -L$("$brew" --prefix isl)/lib -L$("$brew" --prefix zlib)/lib -L$("$brew" --prefix expat)/lib"
export MACOSX_DEPLOYMENT_TARGET=15.0

mkdir -p "$work_root" "$output_root"
if [[ "$resume" != 1 ]]; then
  git clone --branch "$source_ref" --depth 1 --single-branch \
    https://github.com/riscv-mcu/riscv-gnu-toolchain.git "$source_root"
  test "$(git -C "$source_root" rev-parse HEAD)" = "$source_commit"
  git -C "$source_root" submodule update --init --depth 1 \
    riscv-binutils riscv-gcc riscv-newlib
fi
test "$(git -C "$source_root" rev-parse HEAD)" = "$source_commit"
test "$(git -C "$source_root/riscv-gcc" rev-parse HEAD)" = "$gcc_commit"
test "$(git -C "$source_root/riscv-binutils" rev-parse HEAD)" = "$binutils_commit"
test "$(git -C "$source_root/riscv-newlib" rev-parse HEAD)" = "$newlib_commit"
test "$(tr -d '\r\n' < "$source_root/riscv-gcc/gcc/BASE-VER")" = "$toolchain_version"

patch_config_sub() {
  local config_sub="$1"
  if "$config_sub" riscv-nuclei-elf >/dev/null 2>&1; then
    return
  fi
  python3 - "$config_sub" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding='utf-8')
replacements = (
    ('| riscv32-* | riscv64-* \\\n', '| riscv-* | riscv32-* | riscv64-* \\\n'),
    ('| riscv32 | riscv64 \\\n', '| riscv | riscv32 | riscv64 \\\n'),
)
changed = 0
for needle, replacement in replacements:
    count = source.count(needle)
    if count > 1:
        raise SystemExit(f'Ambiguous legacy RISC-V machine pattern in {path}')
    if count == 1:
        source = source.replace(needle, replacement)
        changed += 1
if changed == 0:
    raise SystemExit(f'Unable to locate a legacy RISC-V machine pattern in {path}')
with path.open('w', encoding='utf-8', newline='\n') as stream:
    stream.write(source)
PY
  test "$("$config_sub" riscv-nuclei-elf)" = riscv-nuclei-elf
}
for config_sub in \
  "$source_root/riscv-gcc/config.sub" \
  "$source_root/riscv-binutils/config.sub" \
  "$source_root/riscv-newlib/config.sub"; do
  patch_config_sub "$config_sub"
done

# GCC 9 predates Apple Silicon. Add an arm64 Darwin host-hooks object, but keep
# the generic mmap hooks instead of the x86 port's fixed-address PCH mapping.
config_host="$source_root/riscv-gcc/gcc/config.host"
python3 - "$config_host" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding='utf-8')
needle = '''  i[34567]86-*-darwin* | x86_64-*-darwin*)
    out_host_hook_obj="${out_host_hook_obj} host-i386-darwin.o"
    host_xmake_file="${host_xmake_file} i386/x-darwin"
    ;;
'''
replacement = '''  arm*-*-darwin* | aarch64*-*-darwin*)
    out_host_hook_obj="${out_host_hook_obj} host-aarch64-darwin.o"
    host_xmake_file="${host_xmake_file} aarch64/x-darwin"
    ;;
''' + needle
if 'host-aarch64-darwin.o' not in source:
    if source.count(needle) != 1:
        raise SystemExit(f'Unable to locate the Darwin host configuration in {path}')
    with path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(source.replace(needle, replacement))
PY
cp "$source_root/riscv-gcc/gcc/config/i386/host-i386-darwin.c" \
  "$source_root/riscv-gcc/gcc/config/aarch64/host-aarch64-darwin.c"
python3 - "$source_root/riscv-gcc/gcc/config/aarch64/host-aarch64-darwin.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding='utf-8')
source = source.replace('#include "config/host-darwin.h"\n', '')
needle = '#include "hosthooks-def.h"\n'
note = '''#include "hosthooks-def.h"

/* Upstream GCC disables host PCH on arm64 Darwin. GCC 9 predates that
   configure switch, so keep the generic mmap hooks instead of the legacy
   Darwin fixed 1 GiB PCH reservation, which is invalid on Apple Silicon.  */
'''
if source.count(needle) != 1:
    raise SystemExit(f'Unable to locate the host-hooks include in {path}')
source = source.replace(needle, note)
with path.open('w', encoding='utf-8', newline='\n') as stream:
    stream.write(source.replace('i386-darwin', 'aarch64-darwin'))
PY
cat > "$source_root/riscv-gcc/gcc/config/aarch64/x-darwin" <<'MAKE'
host-aarch64-darwin.o : $(srcdir)/config/aarch64/host-aarch64-darwin.c
	$(COMPILE) $<
	$(POSTCOMPILE)
MAKE

# libcc1 is GDB's compile-integration plugin and is not used by this bare-metal
# Arduino toolchain. GCC 9's plugin cannot link on arm64 Darwin because plugin
# symbols are resolved dynamically on the supported legacy hosts. Disable it,
# and disable libstdc++ PCH, in the actual stage-2 GCC configure invocation.
patch_stage2_configure() {
  local makefile="$1"
  python3 - "$makefile" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding='utf-8')
marker = 'stamps/build-gcc-newlib-stage2:'
start = source.find(marker)
if start < 0:
    raise SystemExit(f'Unable to locate the stage-2 GCC recipe in {path}')
end = source.find('\n#\n# MUSL', start)
if end < 0:
    raise SystemExit(f'Unable to locate the end of the stage-2 GCC recipe in {path}')
section = source[start:end]
needle = '\t\t--disable-nls \\\n'
options = '\t\t--disable-nls \\\n\t\t--disable-libcc1 \\\n\t\t--disable-libstdcxx-pch \\\n'
if '--disable-libcc1' not in section:
    if section.count(needle) != 1:
        raise SystemExit(f'Unable to patch the stage-2 GCC configure flags in {path}')
    section = section.replace(needle, options)
    source = source[:start] + section + source[end:]
    with path.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(source)
PY
}
patch_stage2_configure "$source_root/Makefile.in"

if [[ "$resume" != 1 ]]; then
  mkdir "$build_root"
fi
test -d "$build_root"
cd "$build_root"
if [[ "$resume" != 1 ]]; then
  "$source_root/configure" \
    --prefix="$prefix" \
    --with-arch=rv32gc \
    --with-abi=ilp32d \
    --enable-multilib \
    --disable-gdb \
    --disable-libstdcxx-pch \
    --disable-gcc-checking
else
  patch_stage2_configure "$build_root/Makefile"
fi
make -j"$(sysctl -n hw.logicalcpu)" \
  NEWLIB_TUPLE=riscv-nuclei-elf \
  BINUTILS_TARGET_FLAGS_EXTRA=--with-system-zlib

host_lib="$prefix/lib/host"
mkdir -p "$host_lib"
changed=1
while [[ "$changed" -eq 1 ]]; do
  changed=0
  while IFS= read -r -d '' binary; do
    if ! file "$binary" | grep -q 'Mach-O'; then
      continue
    fi
    while IFS= read -r dependency; do
      case "$dependency" in
        /opt/homebrew/*|/usr/local/*)
          name="$(basename "$dependency")"
          if [[ ! -f "$host_lib/$name" ]]; then
            cp -L "$dependency" "$host_lib/$name"
            chmod u+w "$host_lib/$name"
            changed=1
          fi
          install_name_tool -change "$dependency" "@rpath/$name" "$binary"
          ;;
      esac
    done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
  done < <(find "$prefix" -type f -print0)
done

while IFS= read -r -d '' binary; do
  if ! file "$binary" | grep -q 'Mach-O'; then
    continue
  fi
  relative="$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$host_lib" "$(dirname "$binary")")"
  install_name_tool -add_rpath "@loader_path/$relative" "$binary" 2>/dev/null || true
done < <(find "$prefix" -type f -print0)
while IFS= read -r -d '' library; do
  install_name_tool -id "@rpath/$(basename "$library")" "$library"
done < <(find "$host_lib" -type f -name '*.dylib' -print0)

unresolved="$(find "$prefix" -type f -print0 | while IFS= read -r -d '' binary; do
  if file "$binary" | grep -q 'Mach-O'; then
    otool -L "$binary" | tail -n +2 | awk '{print $1}' | grep -E '^(/opt/homebrew|/usr/local)/' || true
  fi
done)"
test -z "$unresolved"

while IFS= read -r -d '' binary; do
  if file "$binary" | grep -q 'Mach-O'; then
    codesign --force --sign - "$binary"
  fi
done < <(find "$prefix" -type f -print0)

gcc="$prefix/bin/riscv-nuclei-elf-gcc"
test "$("$gcc" -dumpversion)" = "$toolchain_version"
test "$("$gcc" -dumpmachine)" = riscv-nuclei-elf
test "$("$gcc" --print-multi-lib | wc -l | awk '{print $1}')" = 20
"$gcc" --print-multi-lib | grep -Fq 'rv32imafc/ilp32f'
printf '%s\n' 'int main(void) { return 0; }' > "$work_root/smoke.c"
"$gcc" -march=rv32imafc -mabi=ilp32f -Os --specs=nano.specs \
  "$work_root/smoke.c" -o "$work_root/smoke.elf"

printf '%s\n' \
  "source=https://github.com/riscv-mcu/riscv-gnu-toolchain/tree/$source_commit" \
  "source_ref=$source_ref" \
  "source_commit=$source_commit" \
  "gcc_commit=$gcc_commit" \
  "binutils_commit=$binutils_commit" \
  "newlib_commit=$newlib_commit" \
  'target=riscv-nuclei-elf' \
  "gcc=$toolchain_version" \
  'host=arm64-apple-darwin' \
  "builder=$("$CC" -dumpfullversion)" \
  "macos=$(sw_vers -productVersion)" \
  > "$prefix/BUILD-INFO.txt"

package_root="$work_root/package"
mkdir "$package_root"
cp -R "$prefix" "$package_root/riscv-gcc"
archive="$output_root/riscv-nuclei-elf-gcc-$toolchain_version-macos-arm64.tar.gz"
COPYFILE_DISABLE=1 tar -czf "$archive" -C "$package_root" riscv-gcc
cd "$output_root"
shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256"
printf 'Archive: %s\n' "$archive"
cat "$(basename "$archive").sha256"
