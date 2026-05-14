# Terminal Emoji Challenges in `st`

Terminal emulators use a rigid grid where every character takes up either one or two columns. Modern emojis often do not fit this system perfectly, leading to three common problems in `st`.

## 1. Character Width Mismatch (Overlap)

### The Problem
To decide how many columns a character needs, `st` asks the operating system via a function called `wcwidth()`. Many system tables are outdated and mark modern emojis as 1 column wide. 

However, emoji fonts draw these symbols as squares, which actually require 2 columns. Because `st` only reserves 1 column, the next character is drawn directly on top of the right half of the emoji.

### The Fix
We use a custom `st_wcwidth()` function to intercept these lookups. If a character is an emoji, we manually return a width of 2, regardless of what the system says. This ensures `st` reserves enough space.

## 2. Greedy Fallback Fonts (Incorrect Digits)

### The Problem
When the main font is missing a character, `st` looks for it in a fallback emoji font. Many emoji fonts also contain standard characters like numbers and spaces. 

If `st` starts using an emoji font, it often continues using it for the numbers that follow. This results in standard text looking strange because it is being rendered by the emoji font instead of the primary monospace font.

### The Fix
In the rendering code (`x.c`), we add a check. Before letting a fallback font handle a character, we ask if the primary font can handle it first. If the primary font has the character, the renderer switches back immediately.

## 3. Terminal and App Desync (Vim Redraw Issue)

### The Problem
This occurs when `st` and an application like Vim disagree on a character's width. For example, `st` might see a symbol as 2 columns wide, while Vim thinks it is only 1.

### The Vim Example
1. `st` treats an emoji as 2 columns, marking the second column as a "dummy" cell to protect the emoji's right half.
2. Vim thinks the emoji is 1 column, so it assumes the second column is empty. 
3. When you highlight a line in Vim, it tries to draw a "background-colored space" in that second column.
4. `st` sees an application trying to write to a protected dummy cell. To prevent visual corruption, `st`'s default behavior is to delete the emoji in the first column. This makes the emoji disappear when highlighted.

### The Fix
We modify `term_set_char()` to be more flexible. If an application tries to write a space into a dummy cell, `st` now accepts the background color attributes but preserves the emoji's dummy state. This keeps the emoji visible even when Vim and `st` disagree on the width.

```c
if (term.lines[y][x].mode & ATTR_WDUMMY) {
    if (u == ' ') {
        /* Accept the background color/attribute but keep the 
           dummy state so the emoji is not deleted. */
        term.dirts[y] = true;
        term.lines[y][x] = *attr;
        term.lines[y][x].rune = '\0';
        term.lines[y][x].mode = attr->mode | ATTR_WDUMMY;
        return;
    }
    /* If it is not a space, delete the emoji to avoid corruption. */
}
```
