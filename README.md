# tiny-editor
![image](https://github.com/ghkdxofla/tiny-editor/assets/26355065/bbaa4184-eab2-496e-95c9-b78c15752eb0)

## Compile
```bash
cd src
make
```

## Run
```bash
./tiny
```
## Good to know
### ASCII
- ASCII codes `0–31` are all control characters, and `127` is also a control character. ASCII codes `32–126` are all printable.
- `Arrow keys`, `Page Up`, `Page Down`, `Home`, and `End` all input 3 or 4 bytes to the terminal: `27`, `[`, and then one or two other characters. This is known as an escape sequence. All escape sequences start with a 27 byte. Pressing Escape sends a single 27 byte as input.
- `Backspace` is byte `127`. `Delete` is a 4-byte escape sequence.
- `Enter` is byte 10, which is a newline character, also known as `\n`.
- `Ctrl-A` is 1, `Ctrl-B` is 2, `Ctrl-C` is… oh, that terminates the program, right. But the Ctrl key combinations that do work seem to map the letters A–Z to the codes 1–26.

### Key Combinations
- `Ctrl-S`, you may find your program seems to be frozen. What you’ve done is you’ve asked your program to stop sending you output. 
- `Ctrl-Q` to tell it to resume sending you output.
- `Ctrl-Z` (or maybe `Ctrl-Y`), your program will be suspended to the background. Run the `fg` command to bring it back to the foreground. (It may quit immediately after you do that, as a result of read() returning -1 to indicate that an error occurred. This happens on macOS, while Linux seems to be able to resume the read() call properly.)

## Reference
- [rurumimic's editor](https://github.com/rurumimic/editor)
- [kilo's github](https://github.com/antirez/kilo)
- [Writing an editor in less than 1000 lines of code, just for fun](http://antirez.com/news/108)
- [Build Your Own Text Editor](https://viewsourcecode.org/snaptoken/kilo/)
- [Text Editor - Data Structures; Avery Laird](https://www.averylaird.com/programming/the%20text%20editor/2017/09/30/the-piece-table)
- [Text Editor Data Structures; Cameron DaCamara](https://cdacamar.github.io/data%20structures/algorithms/benchmarking/text%20editors/c++/editor-data-structures/)
- [Writing Text Editor; Tsoding Daily](https://youtu.be/2UY_Am-Q-oI)
