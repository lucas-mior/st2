#!/bin/sh

urlregex="(((http|https|gopher|gemini|ftp|ftps|git)://|www\.)[a-zA-Z0-9.]*[:]?[a-zA-Z0-9./@$&%?$\#=_~-]*)|((magnet:\?xt=urn:btih:)[a-zA-Z0-9]*)"
urls=$(sed 's/.*│//g' | tr -d '\n' | grep -aEo "$urlregex" | uniq | sed "s/\(\.\|,\|;\|\!\|\?\)$//; s/^www./http:\/\/www\./")
[ -z "$urls" ] && exit 1
if [ "$2" = "o" ]; then
  chosen=$(echo "$urls" | dmenu -w "$1" -i -p 'Follow which url?' -l 10)
  [ -z "$chosen" ] && exit 0
  echo "$chosen" | tr -d '\n' | xclip -selection clipboard
  setsid xdg-open "$chosen" >/dev/null 2>&1 &
else
  echo "$urls" | dmenu -w "$1" -p 'Copy which url?' -l 10 | tr -d '\n' | xclip -selection clipboard
fi
