#!/bin/sh

# shellcheck disable=SC2086

target="${1:-build}"
CC=${CC:-cc}

VERSION="0.9.3"

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
# CFLAGS="$CFLAGS -Werror"
CFLAGS="$CFLAGS -Wno-padded"
CFLAGS="$CFLAGS -Wno-format-pedantic"
CFLAGS="$CFLAGS -Wno-unknown-warning-option"
CFLAGS="$CFLAGS -Wno-gnu-union-cast"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-undefined-internal"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-unknown-pragmas"
CFLAGS="$CFLAGS -Wno-type-limits"

if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-ignored-attributes"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
    CFLAGS="$CFLAGS -Wno-c23-extensions"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-assign-enum"
fi

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
MANPREFIX="${MANPREFIX:-$PREFIX/share/man}"

PKG_CONFIG=${PKG_CONFIG:-pkg-config}

INCS="$($PKG_CONFIG --cflags fontconfig) \
  $($PKG_CONFIG --cflags freetype2)"

LIBS="-lm -lrt -lX11 -lutil -lXft -lImlib2 $($PKG_CONFIG --libs fontconfig) $($PKG_CONFIG --libs freetype2)"

STCPPFLAGS="-DVERSION="\"$VERSION\"" -D_XOPEN_SOURCE=600"
STCFLAGS="$INCS $STCPPFLAGS $CPPFLAGS $CFLAGS"
STLDFLAGS="$LIBS $LDFLAGS"

ctags --kinds-C=+l+d ./*.h ./*.c 2> /dev/null || true
vtags.sed tags > .tags.vim 2> /dev/null || true

case "$target" in
clean)
	set -x
	rm -f st st-${VERSION}.tar.gz
	;;

build|all)
	set -x
	$CC -o st main.c $STCFLAGS $STLDFLAGS
	;;

install)
	[ ! -f st ] && "$0" build
	set -x
	mkdir -p ${DESTDIR}${PREFIX}/bin
	install -Dm755 st ${DESTDIR}${PREFIX}/bin/st
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < st.1 > ${DESTDIR}${MANPREFIX}/man1/st.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/st.1
	tic -sx st.info
	echo "Please see the README regarding the terminfo entry of st."
	;;

uninstall)
	set -x
	rm -f ${DESTDIR}${PREFIX}/bin/st
	rm -f ${DESTDIR}${MANPREFIX}/man1/st.1
	;;

*)
	echo "usage: $0 [ build | all | install | uninstall | clean | dist ]"
	;;
esac
