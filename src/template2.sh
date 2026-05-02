#!/bin/sh

if [ -z "$1" ]; then
	echo "usage: $0 <file.c>"
	exit 1
fi
name=$(echo "$1" | sed -E 's/\.c//g')
# name=$(echo "$1" | sed -E 's/\.c//g' | tr '[:lower:]' '[:upper:]')
echo "name=$name"

printf '
#if TESTING_@@@@

#include <stdbool.h>
#include <stdlib.h>

#include "assert.c"

int
main(void) {
	ASSERT(true);
	exit(EXIT_SUCCESS);
}

#endif /* TESTING_@@@@ */
' | sed "s/@@@@/$name/g" >> "$1"
