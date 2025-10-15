#!/bin/sh

# shellcheck disable=SC2086

target="${1:-build}"
CC=${CC:-cc}
CC=clang

VERSION="0.9.3"

CFLAGS="$CFLAGS -Wall -Wextra " #-Werror -ferror-limit=1"
CFLAGS="$CFLAGS -Wno-unused-parameter -fsanitize=undefined -g"
CFLAGS="$CFLAGS -Wno-unused-variable -Wno-unused-macros -Wno-type-limits"
CFLAGS="$CFLAGS -Wno-missing-field-initializers -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-format-nonliteral -Wno-implicit-fallthrough"
if [ "$CC" = "clang" ]; then
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage -Wno-padded"
fi

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-/}"
MANPREFIX="${MANPREFIX:-$PREFIX/share/man}"

PKG_CONFIG=${PKG_CONFIG:-pkg-config}

INCS="$($PKG_CONFIG --cflags fontconfig) \
  $($PKG_CONFIG --cflags freetype2)"

LIBS="-lm -lrt -lX11 -lutil -lXft $($PKG_CONFIG --libs fontconfig) $($PKG_CONFIG --libs freetype2)"

STCPPFLAGS="-DVERSION="\"$VERSION\"" -D_XOPEN_SOURCE=600"
STCFLAGS="$INCS $STCPPFLAGS $CPPFLAGS $CFLAGS"
STLDFLAGS="$LIBS $LDFLAGS"

echo "target=$target"

case "$target" in
    clean)
        set -x
        rm -f st st-${VERSION}.tar.gz
        ;;

    build|all)
        set -x
        $CC -o st x.c $STCFLAGS $STLDFLAGS
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
