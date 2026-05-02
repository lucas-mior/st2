#!/bin/sh

if [ -z "$1" ]; then
	echo "usage: $0 <file.c>"
	exit 1
fi
name=$(echo "$1" | sed -E 's/\.c//g')
# name=$(echo "$1" | sed -E 's/\.c//g' | tr '[:lower:]' '[:upper:]')
echo "name=$name"

printf '
#if defined(__INCLUDE_LEVEL__) && (__INCLUDE_LEVEL__ == 0)
#define TESTING_@@@@ 1
#elif !defined(TESTING_@@@@)
#define TESTING_@@@@ 0
#endif
' | sed "s/@@@@/$name/g" >> "$1"
