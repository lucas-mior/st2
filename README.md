# st2
Terminal emulator for X based on simple terminal

## Changes from original st
- Complete refactor
- Comprehensive testing
- Support for multi code point glyphs
- ./build.sh instead of Makefile
- A bunch of patches:
  * scrollback
  * reflow
  * sixel
  * vim select (which uses vim so it stays in sync with your vim configuration)
  * others

## Installation
```sh
git clone https://github.com/lucas-mior/st2
cd st2
./build.sh
sudo ./build.sh install
```

## Why not other terminals?
I want a terminal that has all the features that I need, plus has a code base
small enough that a single human can understand and modify it.

### Why not st from suckless?
It lacks too many features and the source code is unreadable unless you
were the one who wrote it.

### Why not urxvt?
urxvt does not reflow, and it is notoriously bad at handling unicode and the
code base is huge.

### Why not alacritty?
Alacritty does not compensate for bugs in emoji fonts (see emoji_bugs/)
and the code base is huge.

### Why not kitty?
It takes long to open and gives you a prompt when you try to close it and the
code base is huge and it does not support sixels.

### Why not contour?
It takes forever to open and the code base is huge.
