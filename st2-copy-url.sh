#!/bin/sh

protocols="(https?|gopher|gemini|ftps?|git)"
domain="[a-zA-Z0-9.-]+(:[0-9]+)?"
urlpath="[][a-zA-Z0-9./:@$&%?+,#=_~!;*'()-]*"
normal="(($protocols://|www\.)$domain$urlpath)"
urlregex="$normal|(magnet:\?xt=urn:btih:[a-zA-Z0-9]+)"

urls=$(sed 's/.*│//g' \
       | sed -E ':a;N;s/\n[[:space:]]*([+%&?=/#._~:@,-])/\1/;ta;P;D' \
       | tr '\n' ' ' \
       | grep -aEo "$urlregex" \
       | sort -u \
       | sed "s/\(\.\|,\|;\|\!\|\?\)$//; s/^www./http:\/\/www\./")

if [ -z "$urls" ]; then
    exit 1
fi

chosen=$(echo "$urls" | dmenu -w "$1" -i -p 'Follow which url?' -l 10)

if [ -z "$chosen" ]; then
    exit 0
fi

printf "%s" "$chosen" \
    | xclip -selection clipboard

setsid xdg-open "$chosen" >/dev/null 2>&1 &
