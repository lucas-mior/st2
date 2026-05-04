#!/bin/sh -e

# shellcheck disable=SC2086

if [ "${1:-}" != "--parsed" ]; then
    # Filter out lines starting with '+' followed by '[' or '[['
    pattern="^\+ \[.+\]"

    # 1. Open FD 3 and point it to the current stdout (1).
    # 2. Redirect the command's stderr (2) to stdout (1) so it enters the pipe.
    # 3. Redirect the command's stdout (1) to FD 3 so it bypasses the pipe.
    # 4. The pipe now only contains the original stderr.
    # 5. grep filters the stream and sends it back to stderr (2).
    { "$0" --parsed "$@" 2>&1 1>&3 | grep -Ev "$pattern" >&2; } 3>&1

    # Note: In POSIX, $? here will be the exit code of grep, not $0.
    exit $?
fi
shift

set -e

error () {
    >&2 printf "$@"
    return
}

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

if command -v measure; then
    measure=$(which measure)
else
    measure=""
fi

if [ -n "$BASH_VERSION" ]; then
    # shellcheck disable=SC3044
    shopt -s expand_aliases
fi

dir=$(dirname "$(readlink -f "$0")")
cd "$dir"
cbase="cbase"
CPPFLAGS="$CPPFLAGS -I$dir/$cbase"
CPPFLAGS="$CPPFLAGS -I."

mkdir -p gen
{ cat st-copy-output.sh; printf '\0'; } \
    | xxd -i -n st_copy_output | sed 's/unsigned/static unsigned/' > gen/copy_output.h
{ cat st-copy-url.sh;    printf '\0'; } \
    | xxd -i -n st_copy_url    | sed 's/unsigned/static unsigned/' > gen/copy_url.h

CPPFLAGS="$CPPFLAGS -I$dir/gen"

cd "$dir" || exit
program=$(basename "$(readlink -f "$dir")")
script=$(basename "$0")

. ./targets
target="${1:-build}"

if ! grep -q "$target" ./targets; then
    echo "usage: $script <targets>"
    cat targets
    exit 1
fi

printf "\n${script} ${RED}${1} ${2}$RES\n"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"

main="main.c"
exe="bin/$program"
mkdir -p "$(dirname "$exe")"

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=600"
CPPFLAGS="$CPPFLAGS -DGETTEXT_PACKAGE=$program"
CPPFLAGS="$CPPFLAGS -DLOCALEDIR=$PREFIX/share/locale"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wall -Wextra"
if [ -z "$NOCOLORS" ]; then
    CFLAGS="$CFLAGS -fdiagnostics-color=always"
fi
# CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wno-format-pedantic"
CFLAGS="$CFLAGS -Wno-unknown-warning-option"
CFLAGS="$CFLAGS -Wno-gnu-union-cast"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-type-limits"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-deprecated-declarations"
CFLAGS="$CFLAGS -Wno-unknown-pragmas"
CFLAGS="$CFLAGS -Wno-format-security"
CFLAGS="$CFLAGS -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-comma"
CFLAGS="$CFLAGS -Wno-undef"
CFLAGS="$CFLAGS -Wno-cast-align"
CFLAGS="$CFLAGS -Wno-bad-function-cast"

CPPFLAGS="$CPPFLAGS $(pkg-config --cflags fontconfig)"
CPPFLAGS="$CPPFLAGS $(pkg-config --cflags freetype2)"

LDFLAGS="$LDFLAGS -lm -lrt -lX11 -lutil -lXft -lImlib2"
LDFLAGS="$LDFLAGS $(pkg-config --libs fontconfig)"
LDFLAGS="$LDFLAGS $(pkg-config --libs freetype2)"

CC="${CC:-cc}"

option_remove() {
    echo "$1" | sed -E "s| *$2 +| |g"
}

with_other () {
    compiler="$1"
    compiler_macro=$(echo "$compiler" | tr '[:lower:]' '[:upper:]')
    compiler_macro="__${compiler_macro}__"
    shift
    args="$*"
    trace_on
    while ! problem=$($compiler "-D${compiler_macro}" $args 2>&1); do
        trace_off
        problem=$(echo "$problem" | head -n 1 | tr -d "'")

        sleep 0.4
        if echo "$problem" | grep -Eq "unknown (argument|option)"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        elif echo "$problem" | grep -q "unknown file extension:"; then
            arg=$(echo "$problem" | awk '{print $NF}')
            printf "\nRemoving argument $arg...\n"
            args=$(option_remove "$args" "$arg")
        else
            printf "\n\nError compiling with $compiler:\n\n%s" "${problem}\n\n"
            return 1
        fi
        printf "\n"
        trace_on
    done
    return 0
}

if [ "$target" = "cross" ]; then
    cross="$2"
    CC="zig cc"
    CFLAGS="$CFLAGS -target $cross"
    CFLAGS=$(option_remove "$CFLAGS" "-D_GNU_SOURCE")

    case $cross in
    "x86_64-macos"|"aarch64-macos")
        CFLAGS="$CFLAGS -fno-lto"
        ;;
    *windows*)
        exe="bin/$program.exe"
        ;;
    esac
fi

case "$target" in
"debug")
    CFLAGS="$CFLAGS -g3 -fsanitize-trap=undefined"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    exe="bin/${program}_debug"
    ;;
"perf")
    CFLAGS="$CFLAGS -g -O2 -flto"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    exe="bin/${program}_perf"
    ;;
"valgrind")
    CFLAGS="$CFLAGS -g3 -O0 -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE -DDEBUGGING=1"
    ;;
"callgrind")
    CFLAGS="$CFLAGS -g3 -O2 -ftree-vectorize"
    CPPFLAGS="$CPPFLAGS $GNUSOURCE"
    ;;
"test")
    CFLAGS="$CFLAGS -g3 $GNUSOURCE -DDEBUGGING=1 -fsanitize-trap=undefined -Wno-address"
    ;;
"check")
    CC=gcc
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer"
    ;;
"build"|"run")
    CFLAGS="$CFLAGS $GNUSOURCE -O2 -flto -march=native -ftree-vectorize"
    ;;
"release")
    CFLAGS="$CFLAGS $GNUSOURCE -DRELEASING=1 -O2 -flto -march=native -ftree-vectorize"
    ;;
"fast_feedback")
    CC=clang
    CFLAGS="$CFLAGS $GNUSOURCE -Werror"
    ;;
*)
    CFLAGS="$CFLAGS -O2"
    ;;
esac

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-pedantic"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-documentation"
    CFLAGS="$CFLAGS -Wno-documentation-unknown-command"
    CFLAGS="$CFLAGS -Wno-padded"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
    CFLAGS="$CFLAGS -Wno-double-promotion"

    # to avoid using -Wno-unused-function
    CFLAGS="$CFLAGS -Wno-unneeded-internal-declaration"

    # only for the LSP. It does not understand unity builds
    CFLAGS="$CFLAGS -Wno-undefined-internal"
fi

case "$target" in
"fast_feedback")
    trace_on
    $CC $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS && "$exe"
    trace_off
    ;;
"build"|"debug"|"run"|"release"|"valgrind"|"callgrind"|"perf"|"profile"|"cross")
    trace_on

    ctags --kinds-C=+l+d cbase/*.c src/*.h src/*.c  2> /dev/null || true
    vtags.sed tags | sort | uniq > .tags.vim 2> /dev/null || true
    if [ "$CC" = "chibicc" ]; then
        CPPFLAGS="$CPPFLAGS -D__attribute=__attribute__"
        with_other chibicc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    elif [ "$CC" = "cproc" ]; then
        CPPFLAGS="$CPPFLAGS -D__attribute=__attribute__"
        with_other cproc $CPPFLAGS $CFLAGS src/main.c -o $exe $LDFLAGS
    else
        $measure $CC          $CPPFLAGS $CFLAGS src/main.c -o "$exe" $LDFLAGS
        $CC $CPPFLAGS $CFLAGS -Wno-unused-variable \
            src/test_resize_scroll.c -o bin/test_resize_scroll $LDFLAGS
    fi

    if [ $target = "debug" ]; then
		gdb $exe -ex run 2>&1 | tee "gdb_output_$(date +%s).txt"
    fi
    if [ $target = "run" ]; then
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
"test")
    find . -iname "*.c" | sort | while read -r src; do
        trace_off
        name=$(basename "$src")

        if [ -n "$2" ] && [ "$name" != "$2" ]; then
            continue
        fi
        if [ "$name" = "$main" ]; then
            continue
        fi
        if echo "$src" | grep -q "stc/"; then
            continue
        fi
        if echo "$src" | grep -q "cbase/"; then
            continue
        fi

        name=$(echo "$name" | sed 's/\.c//')
        test_exe="/tmp/${name}_test"

        printf "\nTesting ${RED}${src}${RES} ...\n"

        flags="$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$src")"
        if [ $src = "src/windows_functions.c" ]; then
            if ! zig version; then
                continue
            fi
            CC="zig cc"
            cmdline="zig cc $CPPFLAGS $CFLAGS"
            cmdline=$(option_remove "$cmdline" "-D_GNU_SOURCE")
            cmdline="$cmdline -target x86_64-windows-gnu"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1"
            cmdline="$cmdline $flags -o $test_exe $src"
        else
            cmdline="$CC $CPPFLAGS $CFLAGS"
            cmdline="$cmdline -Wno-unused-variable -DTESTING_$name=1 -DTESTING=1 $LDFLAGS"
            cmdline="$cmdline $flags -o $test_exe $src"
        fi

        if [ "$CC" = "chibicc" ] || [ "$CC" = "cproc" ]; then
            cmdline_no_cc=$(option_remove "$cmdline" "$CC")
            trace_on
            if with_other "$CC" "$cmdline_no_cc"; then
                /tmp/${name}_test
            else
                exit 1
            fi
        else
            trace_on
            if $cmdline; then
                if ! $test_exe; then
                    gdb --quiet \
                        -ex 'break exit' -ex run -ex backtrace -ex quit \
                        $test_exe 2>&1 | tee /dev/tty | xsel -b
                    exit 1
                fi
            else
                exit 1
            fi
        fi
        trace_off
    done
    exit
    ;;
uninstall)
	set -x
	rm -f ${DESTDIR}${PREFIX}/bin/st
	rm -f ${DESTDIR}${MANPREFIX}/man1/st.1
	;;
esac

case "$target" in
"valgrind")
    vg_flags="$vg_flags --error-exitcode=1"
    vg_flags="$vg_flags --leak-check=no"
    # vg_flags="$vg_flags --show-leak-kinds=definite"
    # vg_flags="$vg_flags --errors-for-leak-kinds=definite"
    vg_flags="$vg_flags --track-origins=yes"
    # vg_flags="$vg_flags --suppressions=valgrind.supress"
    # vg_flags="$vg_flags --gen-suppressions=yes"
    vg_flags="$vg_flags --main-stacksize=18388608"

    trace_on
    valgrind $vg_flags -s --tool=memcheck bin/$program 2>&1 \
        | tee "valgrind_output_$(date +%s).txt"
    trace_off
    exit
    ;;
"callgrind")
    out="callgrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=callgrind --callgrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
"cachegrind")
    out="cachegrind_$(date +%s).callgrind"
    trace_on
    valgrind --tool=cachegrind --cachegrind-out-file="$out" bin/$program
    kcachegrind "$out"
    trace_off
    exit
    ;;
"check")
    CC=gcc CFLAGS="-fanalyzer" ./build.sh 2>&1 \
        | sed -E 's/\[[0-9;]*[mK]//g' \
          | tee "gcc-analyzer-$(date +%s).txt"
    setsid -f \
        scan-build --view -analyze-headers --status-bugs ./build.sh 2>&1 \
        | sed -E 's/\[[0-9;]*[mK]//g' \
          > "scan-build-$(date +%s).txt" &
    exit
    ;;
"perf")
    trace_on
    perf record -F 999 -g --call-graph dwarf -o bin/perf.data "$exe"
    perf report -n -g --input bin/perf.data
    trace_off
    exit
    ;;
esac

trace_off
if [ "$target" = "test_all" ]; then
    printf '%s\n' "$targets" | while IFS= read -r target; do
        echo "$target" | grep -Eq "^(# |$)" && continue
        if echo "$target" | grep "cross"; then
            $0 $target
            continue
        fi
        for compiler in gcc tcc clang "zig cc" ; do
            CC=$compiler $0 $target || exit
        done
    done
fi
