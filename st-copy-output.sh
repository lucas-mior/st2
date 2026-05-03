#!/bin/sh

tmpfile=$(mktemp /tmp/st-cmd-output.XXXXXX)
trap 'rm "$tmpfile"' EXIT HUP TERM

cat > "$tmpfile"
sed -i 's/\x0//g' "$tmpfile"

ps1=$(grep "\S" "$tmpfile" \
          | tail -n 1 \
          | sed 's/^\s*//' \
          | cut -d' ' -f1)

chosen=$(grep -F "$ps1" "$tmpfile" \
             | sed '$ d' \
             | tac \
             | dmenu -w "$1" -p "Copy output?" -i -l 10 \
             | sed 's/[^^]/[&]/g; s/\^/\\^/g')

if [ -n "$chosen" ]; then
  eps1=$(echo "$ps1" | sed 's/[^^]/[&]/g; s/\^/\\^/g')
  awk "/^$chosen$/ {
          p=1;
          print;
          next
      }
      p && /$eps1/ {
          p=0
      };p" "$tmpfile" | xclip -selection clipboard
fi
