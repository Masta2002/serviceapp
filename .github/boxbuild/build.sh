#!/bin/bash
# ServiceApp cross build for an openATV box (Vu+ Uno 4K SE, ARMv7 hard-float)
# against a sysroot made of the image's own feed packages (enigma2-dev,
# python3-dev, libsigc-3.0-dev ... in the versions the box has installed).
# Stages, called one after the other by .github/workflows/box-build.yml:
#   sysroot toolchain build check package
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
W=${GITHUB_WORKSPACE:-$PWD}
FEED=${FEED:-https://feeds2.mynonpublic.com/8.1.0~beta/vuuno4kse}
FEED_ARCH=${FEED_ARCH:-cortexa15hf-neon-vfpv4}
FEEDS="$FEED/$FEED_ARCH,$FEED/all,$FEED/vuuno4kse"
SR=$W/../sysroot
OUT=$W/../out
TC=$W/../tc
CROSS=arm-linux-gnueabihf-
GCCV=14
BOXCC=$TC/boxcc
BOXCXX=$TC/boxcxx
PLUGINDIR=/usr/lib/enigma2/python/Plugins/SystemPlugins/ServiceApp

# what OE passes to every compiler call of the image (TARGET_CC_ARCH with
# the 64-bit time_t / off_t flags: struct layouts must match enigma2's)
OE_CC_FLAGS="-marm -mfpu=neon-vfpv4 -mfloat-abi=hard -mcpu=cortex-a15 -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security"
OE_LDFLAGS="-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed -Wl,-z,relro,-z,now"

export PKG_CONFIG_SYSROOT_DIR=$SR
export PKG_CONFIG_LIBDIR=$SR/usr/lib/pkgconfig:$SR/usr/share/pkgconfig
Q="timeout -k 10 120 qemu-arm -cpu cortex-a15 -L $SR"

notice() { echo "::notice::$*"; }

stage_sysroot() {
  # enigma2-plugin-systemplugins-serviceapp: the image's own build, only to
  # compare with (it is never linked against)
  python3 "$HERE/mksysroot.py" --list "$W/../sysroot.list" "$SR" "$FEEDS" \
    libc6-dev linux-libc-headers-dev libgcc-s-dev libstdc++6 libstdc++-dev \
    enigma2-dev python3-dev libsigc-3.0-dev uchardet-dev openssl-dev libdvbsi++-dev \
    libfreetype-dev libfribidi-dev enigma2-plugin-systemplugins-serviceapp
  wc -l "$W/../sysroot.list"
  du -sh "$SR"
  grep -E '^(enigma2|enigma2-dev|python3-dev|libstdc\+\+6|libsigc-3.0-dev|openssl-dev|enigma2-plugin-systemplugins-serviceapp) ' "$W/../sysroot.list"
  ls "$SR/usr/include/enigma2" | head -20
  cat "$SR/usr/lib/pkgconfig/enigma2.pc"
}

stage_toolchain() {
  mkdir -p "$TC"
  GCCDIR=$(dirname "$(${CROSS}gcc-$GCCV -print-libgcc-file-name)")
  inc="-isystem $GCCDIR/include"
  if [ -d "$GCCDIR/include-fixed" ]; then inc="$inc -isystem $GCCDIR/include-fixed"; fi
  # the C++ headers of the cross compiler (libstdc++ $GCCV, ABI compatible
  # with the image's newer libstdc++.so.6), nothing else of Ubuntu's armhf
  # C library: C headers, start files and libraries come from the sysroot
  cxxinc=$(echo | ${CROSS}g++-$GCCV -x c++ -E -v - 2>&1 \
    | sed -n '/#include <...> search starts here:/,/End of search list./p' \
    | grep '/c++/' | sed 's/^ */-isystem /' | tr '\n' ' ')
  test -n "$cxxinc"
  common="--sysroot=$SR -nostdinc"
  link="-B$SR/usr/lib/ -L$SR/usr/lib -L$SR/lib -Wl,-rpath-link,$SR/usr/lib -Wl,-rpath-link,$SR/lib"
  cat > "$BOXCC" <<EOF
#!/bin/sh
exec ${CROSS}gcc-$GCCV $common $inc -isystem $SR/usr/include $link $OE_CC_FLAGS "\$@"
EOF
  cat > "$BOXCXX" <<EOF
#!/bin/sh
exec ${CROSS}g++-$GCCV $common $cxxinc $inc -isystem $SR/usr/include $link $OE_CC_FLAGS "\$@"
EOF
  chmod +x "$BOXCC" "$BOXCXX"
  cat "$BOXCXX"
  ${CROSS}g++-$GCCV --version | head -1
  cd "$TC"
  cat > hello.cpp <<'EOF'
#include <cstdio>
#include <string>
#include <thread>
#include <optional>
#include <memory>
#include <ctime>
#include <sys/types.h>
#include <gnu/libc-version.h>
static_assert(sizeof(time_t) == 8, "time_t has 64 bits, as in the image");
static_assert(sizeof(off_t) == 8, "off_t has 64 bits");
int main()
{
    std::optional<std::string> s;
    std::thread t([&s] { s = std::string("thread ok"); });
    t.join();
    auto p = std::make_unique<int>(42);
    std::printf("glibc %s at run time, %d.%d headers, %s, %d, libstdc++ %d\n",
                gnu_get_libc_version(), __GLIBC__, __GLIBC_MINOR__, s->c_str(), *p, __GLIBCXX__);
    return 0;
}
EOF
  "$BOXCXX" -std=gnu++20 -O2 -o hello hello.cpp -lpthread -Wl,-Map=hello.map
  grep '^LOAD /' hello.map | awk '{print $2}' | sort -u
  ${CROSS}readelf -d hello | grep NEEDED
  $Q ./hello | tee hello.out
  grep -q 'thread ok, 42' hello.out
  notice "toolchain: $(${CROSS}g++-$GCCV --version | head -1), $(cat hello.out)"
}

stage_build() {
  cd "$W"
  autoreconf -fi > "$W/../autoreconf.log" 2>&1 || { cat "$W/../autoreconf.log"; exit 1; }
  PYINC=$(ls -d "$SR"/usr/include/python3.* | head -1)
  PYVER=${PYINC##*/python}
  echo "Python headers: $PYINC ($PYVER)"
  ./configure --build=x86_64-pc-linux-gnu --host=arm-linux-gnueabihf --prefix=/usr \
    CC="$BOXCC" CXX="$BOXCXX" CFLAGS="-O2 -pipe" CXXFLAGS="-O2 -pipe" LDFLAGS="$OE_LDFLAGS" \
    PYTHON_CPPFLAGS="-I$PYINC" PYTHON_LIBS="-lpython$PYVER" PYTHON_EXTRA_LIBS=" " PYTHON_EXTRA_LDFLAGS=" " \
    || { tail -120 config.log; exit 1; }
  defs=$(sed -n 's/^DEFS = //p' src/serviceapp/Makefile)
  echo "DEFS: $defs"
  case " $defs " in *"-DOPENPLI_ISERVICE_VERSION=2 "*) ;; *) echo "::error::iservice probe: not version 2"; exit 1;; esac
  case " $defs " in *"-DHAVE_EPG"*) ;; *) echo "::error::mhw.h not found"; exit 1;; esac
  make -j"$(nproc)" V=1 > "$W/../make.log" 2>&1 || { tail -150 "$W/../make.log"; exit 1; }
  grep -E 'warning:' "$W/../make.log" | sort | uniq -c | sort -rn | head -40 || true
  rm -rf "$OUT/root"
  make install DESTDIR="$OUT/root" > /dev/null
  find "$OUT/root" -type f | sed "s|^$OUT/root||" | sort
}

# undefined symbols of a module that neither the enigma2 binary nor one of
# the libraries loaded into the enigma2 process (NEEDED of enigma2 and of
# the module) defines; weak references are left out
unresolved() {
  local so=$1 libs l n f
  libs="$SR/usr/bin/enigma2"
  for f in "$SR/usr/bin/enigma2" "$so"; do
    for n in $(${CROSS}readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
      for l in "$SR/usr/lib/$n" "$SR/lib/$n"; do
        if [ -e "$l" ]; then libs="$libs $(readlink -f "$l")"; break; fi
      done
    done
  done
  for l in $(echo $libs | tr ' ' '\n' | sort -u); do
    ${CROSS}nm -D --defined-only "$l" 2>/dev/null | awk '{print $3}'
  done | sed 's/@.*//' | sort -u > "$TC/defined.txt"
  ${CROSS}nm -D --undefined-only "$so" | awk '$1 == "U" {print $2}' | sed 's/@.*//' | sort -u > "$TC/undefined.txt"
  comm -23 "$TC/undefined.txt" "$TC/defined.txt"
}

stage_check() {
  ours=$OUT/root$PLUGINDIR/serviceapp.so
  image=$SR$PLUGINDIR/serviceapp.so
  for f in "$image" "$ours"; do
    echo "== $f"
    ls -l "$f"
    ${CROSS}readelf -d "$f" | grep -E 'NEEDED|SONAME|RPATH|RUNPATH' || true
    ${CROSS}readelf -A "$f" | grep -E 'Tag_CPU_arch|Tag_FP_arch|Tag_ABI_VFP_args|Tag_ABI_enum_size|Tag_ABI_align' || true
    echo "newest symbol versions: $(${CROSS}objdump -T "$f" | grep -oE '(GLIBC|GLIBCXX|CXXABI)_[0-9.]+' | sort -uV | awk -F_ '{v[$1]=$0} END {for (k in v) printf "%s ", v[k]}')"
  done
  echo "libstdc++ of the image provides up to: $(${CROSS}objdump -T "$(readlink -f "$SR/usr/lib/libstdc++.so.6")" | grep -oE 'GLIBCXX_[0-9.]+' | sort -uV | tail -1)"
  unresolved "$image" > "$TC/image_unresolved.txt"
  unresolved "$ours" > "$TC/ours_unresolved.txt"
  echo "== unresolved in the image's serviceapp.so ($(wc -l < "$TC/image_unresolved.txt"))"
  c++filt < "$TC/image_unresolved.txt" | head -40
  echo "== unresolved in ours ($(wc -l < "$TC/ours_unresolved.txt"))"
  c++filt < "$TC/ours_unresolved.txt" | head -80
  # every symbol ours needs is in the enigma2 process (the image's own
  # build is the reference: what it leaves open is resolved some other way)
  new=$(comm -23 "$TC/ours_unresolved.txt" "$TC/image_unresolved.txt")
  if [ -n "$new" ]; then
    echo "$new" | c++filt | while read -r s; do echo "::error title=unresolved symbol::$s"; done
    exit 1
  fi
  notice "serviceapp.so: every undefined symbol is defined by enigma2 $(grep '^enigma2 ' "$W/../sysroot.list" | awk '{print $2}') or its libraries"
}

stage_package() {
  ${CROSS}strip --strip-unneeded "$OUT/root$PLUGINDIR/serviceapp.so"
  rev=$(git -C "$W" rev-parse --short HEAD)
  {
    echo "ServiceApp $(sed -n 's/^AC_INIT(\[serviceapp\], \[\([^]]*\)\].*/\1/p' "$W/configure.ac") for openATV ($FEED, $FEED_ARCH)"
    echo "source commit $(git -C "$W" rev-parse HEAD)"
    echo "built $(date -u +%Y-%m-%dT%H:%M:%SZ) by ${GITHUB_SERVER_URL:-}/${GITHUB_REPOSITORY:-}/actions/runs/${GITHUB_RUN_ID:-}"
    echo
    echo "== feed packages of the sysroot"
    awk '{print $1, $2}' "$W/../sysroot.list"
  } > "$OUT/root$PLUGINDIR/BUILDINFO.txt"
  dist=${RUNNER_TEMP:-$OUT}/dist
  mkdir -p "$dist"
  tar -C "$OUT/root" --owner=0 --group=0 --numeric-owner -czf "$dist/serviceapp-box-$rev.tar.gz" "${PLUGINDIR#/}"
  ls -l "$dist"
}

for s in "$@"; do
  echo "::group::stage $s"
  "stage_$s"
  echo "::endgroup::"
done
