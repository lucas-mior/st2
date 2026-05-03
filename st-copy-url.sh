#!/bin/sh

urlregex="(((http|https|gopher|gemini|ftp|ftps|git)://|www\.)[a-zA-Z0-9.]*[:]?[a-zA-Z0-9./@$&%?$\#=_~-]*)|((magnet:\?xt=urn:btih:)[a-zA-Z0-9]*)"

urls=$(sed 's/.*│//g' \
       | tr -d '\n' \
       | grep -aEo "$urlregex" \
       | uniq \
       | sed "s/\(\.\|,\|;\|\!\|\?\)$//; s/^www./http:\/\/www\./")

if [ -z "$urls" ]; then
    exit 1
fi

chosen=$(echo "$urls" | dmenu -w "$1" -i -p 'Follow which url?' -l 10)
if [ -z "$chosen" ]; then
    exit 0
fi
echo "$chosen" \
    | tr -d '\n' \
    | xclip -selection clipboard
setsid xdg-open "$chosen" >/dev/null 2>&1 &
