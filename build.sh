#!/bin/sh -e

# shellcheck disable=SC2086

set -x

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit
# shellcheck source=./cbase/common.sh
. ./cbase/common.sh

program=$(common_get_program "$0")
script=$(basename "$0")
common_build_parse_args "$@"

case "$mode" in
build|cachegrind|callgrind|check)
    ;;
cross|debug|debug-fast|fast_feedback)
    ;;
install|run|test|test_all|uninstall|valgrind)
    ;;
*)
    common_build_unknown_mode
    ;;
esac

mkdir -p gen
script_to_header() {
    var_name=$1

    printf 'static unsigned char %s[] = {\n' "$var_name"
    od -An -tx1 -v \
        | awk '{
            for (i = 1; i <= NF; i += 1) {
                printf "    0x%s,\n", $i
            }
        }'
    printf '};\n'
    printf 'static unsigned int %s_len = sizeof(%s);\n' \
        "$var_name" "$var_name"
}

trace_on
{ cat st-copy-output.sh; printf '\0'; } \
    | script_to_header st_copy_output > gen/copy_output.h
{ cat st-copy-url.sh;    printf '\0'; } \
    | script_to_header st_copy_url > gen/copy_url.h

trace_off
cd "$dir" || exit
common_build_print_invocation "$script"
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
exe="bin/$program"
mkdir -p "$(dirname "$exe")"
CC=$(common_get_compiler "$mode")
case "$(uname -a)" in
*MINGW*|*MSYS*|*CYGWIN*|*mingw*|*msys*|*cygwin*|*windows*)
    ;;
*)
    CFLAGS="$CFLAGS -pthread"
    ;;
esac

CPPFLAGS="$CPPFLAGS -I. -Icbase -Igen"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags x11)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags xft)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags fontconfig)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags freetype2)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags harfbuzz)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags imlib2)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags libutf8proc)"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"

LDFLAGS="$LDFLAGS -lm"
LDFLAGS="$LDFLAGS $(pkg-config --libs x11)"
LDFLAGS="$LDFLAGS $(pkg-config --libs xft)"
LDFLAGS="$LDFLAGS $(pkg-config --libs fontconfig)"
LDFLAGS="$LDFLAGS $(pkg-config --libs freetype2)"
LDFLAGS="$LDFLAGS $(pkg-config --libs harfbuzz)"
LDFLAGS="$LDFLAGS $(pkg-config --libs imlib2)"
LDFLAGS="$LDFLAGS $(pkg-config --libs libutf8proc)"
case "$(uname -a)" in
*FreeBSD*|*NetBSD*|*OpenBSD*)
    LDFLAGS="$LDFLAGS -lutil"
    ;;
esac
if [ "$mode" = "cross" ]; then
    common_build_cross_all
    cross="$target"
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"
    case $cross in
    x86_64-macos|aarch64-macos)
        CFLAGS="$CFLAGS -fno-lto"
        ;;
    *windows*)
        exe="bin/$program.exe"
        ;;
    esac
fi
case "$mode" in
debug)
    CFLAGS="$CFLAGS -g3"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    exe="bin/$program"
    ;;
debug-fast)
    CFLAGS="$CFLAGS -g2 -O2 -flto -march=native -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
valgrind)
    CFLAGS="$CFLAGS -g3 -Og -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1"
    ;;
callgrind)
    CFLAGS="$CFLAGS -g3 -O2 -ftree-vectorize"
    ;;
test)
    CFLAGS="$CFLAGS -g3 -DDEBUGGING=1"
    ;;
check)
    CC=gcc
    CFLAGS="$CFLAGS -DDEBUGGING=1 -fanalyzer"
    ;;
build|run)
    CFLAGS="$CFLAGS -O2 -flto -march=native -ftree-vectorize"
    ;;
fast_feedback)
    ;;
cross)
    CFLAGS="$CFLAGS -O2"
    ;;
esac

case "$mode" in
fast_feedback)
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS && "$exe"
    trace_off
    ;;
build|debug|debug-fast|run|valgrind|callgrind|cross)
    common_build_tags cbase src
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS
    # $CC $CPPFLAGS $CFLAGS -Wno-unused-variable \
    #     src/test_resize_scroll.c -o bin/test_resize_scroll $LDFLAGS
    if [ $mode = "run" ]; then
        $exe
    fi
    trace_off
    ;;
install)
    if [ ! -f bin/st ]; then
        "$0" build
    fi
    set -x
    mkdir -p ${DESTDIR}${PREFIX}/bin
    install -Dm755 bin/st ${DESTDIR}${PREFIX}/bin/st
    mkdir -p ${DESTDIR}${PREFIX}/man/man1
    chmod 644 ${DESTDIR}${PREFIX}/man/man1/st.1
    tic -sx st.info
    echo "Please see the README regarding the terminfo entry of st."
    ;;
test)
    TEST_STDIN=/dev/null \
        common_test "$target"
    exit
    ;;
uninstall)
    set -x
    rm -f ${DESTDIR}${PREFIX}/bin/st
    rm -f ${DESTDIR}${MANPREFIX}/man1/st.1
    ;;
esac
case "$mode" in
valgrind)
    vg_flags="$vg_flags --error-exitcode=1"
    vg_flags="$vg_flags --leak-check=no"
    # vg_flags="$vg_flags --show-leak-kinds=definite"
    # vg_flags="$vg_flags --errors-for-leak-kinds=definite"
    vg_flags="$vg_flags --track-origins=yes"
    # vg_flags="$vg_flags --suppressions=valgrind.supress"
    # vg_flags="$vg_flags --gen-suppressions=yes"
    vg_flags="$vg_flags --main-stacksize=18388608"
    trace_on
    valgrind $vg_flags -s --tool=memcheck bin/$program
    trace_off
    exit
    ;;
callgrind)
    out="callgrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=callgrind --callgrind-out-file="$out" \
        bin/$program -e vim "$HOME/imgs/00teste/test.md"
    kcachegrind "$out"
    trace_off
    exit
    ;;
cachegrind)
    out="cachegrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=cachegrind --cachegrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
check)
    set +e
    CC=gcc CFLAGS="-fanalyzer -fdiagnostics-color=never" ./build.sh
    CFLAGS="--analyze -Xanalyzer -analyzer-output=text"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-werror"
    CFLAGS="$CFLAGS -Xanalyzer -analyzer-opt-analyze-headers"
    CFLAGS="$CFLAGS -Wno-unused-command-line-argument"
    CFLAGS="$CFLAGS -fno-color-diagnostics"
    CC=clang CFLAGS="$CFLAGS" ./build.sh
    exit
    ;;
esac
trace_off
if [ "$mode" = "test_all" ]; then
    common_build_test_all "debug build test" gcc tcc clang "zig cc"
fi
