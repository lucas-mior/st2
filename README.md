# st2
Terminal emulator for X based on simple terminal

## Changes from original st
- Complete refactor
- ./build.sh instead of Makefile
- Testing (WIP)
- A bunch of patches:
  * scrollback
  * reflow
  * sixel
  * vim select (which uses vim so it stays in sync with your vim configuration)
  * others

## Transparent colors 
![ncmpcpp visualizer with transparent colors](https://github.com/lucas-mior/st2/blob/st2/print.gif)

## Installation
```sh
git clone https://github.com/lucas-mior/st2
cd st2
./build.sh
sudo ./build.sh install
```
