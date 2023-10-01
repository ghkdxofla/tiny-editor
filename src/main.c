/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define TINY_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND k with 00011111

enum editorKey {
    ARROW_LEFT = 1000, // use large numbers to avoid conflict with char values
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

/*** data ***/

struct editorConfig {
    int cx, cy; // cursor x, y position
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s); // print error message. perror() looks at the global errno variable and prints a descriptive error message for it.
    exit(1); 
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

/**
 * After the program quits, depending on your shell, you may find your terminal is still not echoing what you type. 
 * Don’t worry, it will still listen to what you type. 
 * Just press Ctrl-C to start a fresh line of input to your shell, and type in reset and press Enter. 
 * This resets your terminal back to normal in most cases. 
 * Failing that, you can always restart your terminal emulator. 
 * We’ll fix this whole problem in the next step.
*/
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // get terminal attributes
    atexit(disableRawMode); // disable raw mode at exit

    struct termios raw = E.orig_termios; // copy terminal attributes

    /**
     * `c_iflag`
     * The c_iflag field is for “input flags”.
     * 
     * `BRKINT`
     * The BRKINT flag will cause a break condition to cause a SIGINT signal to be sent to the program, like pressing Ctrl-C.
     * 
     * `ICRNL`
     * The ICRNL flag fixes Ctrl-M.
     * 
     * `INPCK`
     * The INPCK flag enables parity checking, which doesn’t seem to apply to modern terminal emulators.
     * 
     * `ISTRIP`
     * The ISTRIP flag causes the 8th bit of each input byte to be stripped, meaning it will set it to 0.
     * 
     * `IXON`
     * The IXON flag fixes Ctrl-S and Ctrl-Q.
     * If you run the program now and go through the whole alphabet while holding down Ctrl, 
     * you should see that we have every letter except M. 
     * Ctrl-M is weird: it’s being read as 10, when we expect it to be read as 13, 
     * since it is the 13th letter of the alphabet, and Ctrl-J already produces a 10. 
     * What else produces 10? The Enter key does.
     * It turns out that the terminal is helpfully translating any carriage returns (13, '\r') inputted by the user into newlines (10, '\n').
    */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    
    /**
     * `c_oflag`
     * The c_oflag field is for “output flags”.
     * 
     * `OPOST`
     * The OPOST flag is for “output processing”.
     * It translates '\n' to '\r\n' on output.
     * We’ll fix this later, but for now, we can just turn off output processing altogether.
    */
    raw.c_oflag &= ~(OPOST);

    /**
     * `c_cflag`
     * The c_cflag field is for “control flags”.
     * 
     * `CS8`
     * The CS8 flag sets the character size (CS) to 8 bits per byte.
    */
    raw.c_cflag |= (CS8);

    /**
     * `c_lflag`
     * The c_lflag field is for “local flags”. 
     * A comment in macOS’s <termios.h> describes it as a “dumping ground for other state”. 
     * So perhaps it should be thought of as “miscellaneous flags”. 
     * The other flag fields are c_iflag (input flags), c_oflag (output flags), 
     * and c_cflag (control flags), all of which we will have to modify to enable raw mode.
     *
     * `ECHO`
     * ECHO is a bitflag, defined as 00000000000000000000000000001000 in binary. 
     * We use the bitwise-NOT operator (~) on this value to get 11111111111111111111111111110111.
     * 
     * `ICANON`
     * There is an ICANON flag that allows us to turn off canonical mode. 
     * This means we will finally be reading input byte-by-byte, instead of line-by-line.
     * ICANON comes from <termios.h>. 
     * Input flags (the ones in the c_iflag field) generally start with I like ICANON does. 
     * However, ICANON is not an input flag, it’s a “local” flag in the c_lflag field. 
     * So that’s confusing.
     * 
     * `IEXTEN`
     * IEXTEN is a flag that will allow us to disable Ctrl-V.
     * 
     * `ISIG`
     * ISIG is a flag that allows us to turn off Ctrl-C and Ctrl-Z signals.
    */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // disable echoing of input

    raw.c_cc[VMIN] = 0; // minimum number of bytes of input needed before read() can return
    raw.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // set terminal attributes
}

/**
 * editorReadKey()
 * waits for one keypress and returns it.
*/
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { // read 1 byte from stdin into c
        /**
         * read() returns the number of bytes read, or -1 on error, in which case errno is set appropriately. 
         * Cygwin returns -1 when there is no input available, so we have to check errno to make sure it’s not actually an error.
        */
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        /**
         * `read()`
         * read() attempts to read up to count bytes from file descriptor fd into the buffer starting at buf.
         * if return value is not 1, then we know it’s not an escape sequence, so we return the character as-is.
        */
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; // read 1 byte from stdin into seq[0]
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; // read 1 byte from stdin into seq[1]

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP; // up
                case 'B': return ARROW_DOWN; // down
                case 'C': return ARROW_RIGHT; // right
                case 'D': return ARROW_LEFT; // left
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    /**
     * `\x1b[6n`
     * The escape sequence \x1b[6n reports the cursor position to the program as a series of characters.
    */
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // write 4 bytes to stdout

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break; // read 1 byte from stdin into buf[i]
        if (buf[i] == 'R') break; // stop reading if we encounter 'R'
        i++;
    }
    buf[i] = '\0'; // null terminate buf

    if (buf[0] != '\x1b' || buf[1] != '[') return -1; // check if buf starts with '\x1b['
    
    /**
     * `sscanf()`
     * sscanf() reads data from the string buf and stores them according to the parameter format into the locations given by the additional argument.
     * 
     * `"%d;%d"`
     * %d is for integers. to parse two integers separated by a ;, and put the values into the rows and cols variables.
    */
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // parse buf[2] as int and store in rows and cols

    // char c;

    // while (read(STDIN_FILENO, &c, 1) == 1) { // read 1 byte from stdin into c
    //     /**
    //      * `iscntrl`
    //      * iscntrl() checks whether c is a control character.
    //      * ASCII codes 0–31 are all control characters, and 127 is also a control character. 
    //      * ASCII codes 32–126 are all printable.
    //     */
    //     if (iscntrl(c)) { // is control character
    //         printf("%d\r\n", c);
    //     } else {
    //         printf("%d ('%c')\r\n", c, c);
    //     }
    // }
}

/**
 * getWindowSize()
 * uses the TIOCGWINSZ ioctl to get the size of the terminal window.
 * 
 * The C command (Cursor Forward) moves the cursor to the right,
 * and the B command (Cursor Down) moves the cursor down.
 * The argument says how much to move it right or down by.
 * We use a very large value, 999,
 * which should ensure that the cursor reaches the right and bottom edges of the screen.
 * 
 * `ioctl()`
 * The ioctl() function manipulates the underlying device parameters of special files.
 * 
 * `TIOCGWINSZ`
 * TIOCGWINSZ is an I/O control request that gets the window size of the terminal.
 * 
 * 
*/
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { // get window size.
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        // editorReadKey(); // for debugging that we can observe the results of our escape sequences before the program calls die() and clears the screen
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0} // initialize abuf struct

void abAppend(struct abuf *ab, const char *s, int len) {
    /**
     * `realloc()`
     * The realloc() function changes the size of the memory block pointed to by ptr to size bytes.
     * It extends the size of the block of memory we already have allocated, 
     * or it will take care of free()ing the current block of memory
     * and allocating a new block of memory somewhere else that is big enough for our new string.
    */
    char *new = realloc(ab->b, ab->len + len); // allocate memory for new string

    if (new == NULL) return; // return if realloc fails
    memcpy(&new[ab->len], s, len); // copy string s to end of new string
    ab->b = new; // assign new string to abuf struct
    ab->len += len; // update length of abuf struct
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab) { // draw each row of the buffer to the screen
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {
            char welcome[80];
            int welcomelen = snprintf( // snprintf() returns the number of bytes that would have been written if the buffer had been large enough.
                welcome, 
                sizeof(welcome), 
                "TINY editor -- version %s", 
                TINY_VERSION
            );
            if (welcomelen > E.screencols) welcomelen = E.screencols; // truncate welcome message if it is too long
            int padding = (E.screencols - welcomelen) / 2; // center welcome message
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            abAppend(ab, "~", 1);
        }

        /**
         * `\x1b[K`
         * The escape sequence \x1b[K is a VT100 escape sequence that erases the current line the cursor is on.
         * 2 erases the whole line.
         * 1 erases from the start of the line to the cursor.
         * 0(default) erases from the cursor to the end of the line.
        */
        abAppend(ab, "\x1b[K", 3); // clear line (K; Erase In Line)
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

/**
 * editorRefreshScreen()
 * clears the screen and draws each row of the buffer to the screen.
 * 
 * `write()`
 * write() writes up to count bytes from the buffer pointed buf to the file referred to by the file descriptor fd.
 * 
 * `STDOUT_FILENO`
 * The file descriptor for standard output is 1, so that’s what we pass to write() to write to standard output.
 * 
 * `"\x1b[2J"`
 * The escape sequence \x1b[2J is a VT100 escape sequence that clears the screen.
 * The 4 in our write() call means we are writing 4 bytes out to the terminal.
 * The first byte is \x1b, which is the escape character, or 27 in decimal.
 * (Try and remember \x1b, we will be using it a lot.) The other three bytes are [2J.
 * Escape sequences always start with an escape character (27) followed by a [ character.
 * J command (Erase In Display) to clear the screen.
 * 
 * `"\x1b[H"`
 * The escape sequence \x1b[H positions the cursor at row 1, column 1.
 * if you have an 80×24 size terminal and you want the cursor in the center of the screen,
 * you could use the command <esc>[12;40H.
*/
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    /**
     * `"\x1b[?25l"`
     * The escape sequence \x1b[?25l hides the cursor.
     * some terminals might not support hiding/showing the cursor,
     * but if they don’t, then they will just ignore those escape sequences,
     * which isn’t a big deal in this case.
    */
    abAppend(&ab, "\x1b[?25l", 6); // hide cursor(l; Reset Mode)
    // abAppend(&ab, "\x1b[2J", 4); // clear screen
    abAppend(&ab, "\x1b[H", 3); // reposition cursor

    editorDrawRows(&ab);

    char buf[32];

    /**
     * `snprintf()`
     * snprintf() writes to the character string str.
     * snprintf() appends the terminating null byte ('\0') to the output string.
     * save E.cy + 1 and E.cx + 1 to buf with format "\x1b[%d;%dH" and length of buf
    */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[H", 3); // reposition cursor
    abAppend(&ab, "\x1b[?25h", 6); // show cursor(h; Set Mode)

    write(STDOUT_FILENO, ab.b, ab.len); // write abuf to stdout
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            E.cx--;
            break;
        case ARROW_RIGHT:
            E.cx++;
            break;
        case ARROW_UP:
            E.cy--;
            break;
        case ARROW_DOWN:
            E.cy++;
            break;
    }
}

/**
 * editorProcessKeypress()
 * waits for a keypress and handles it.
*/
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'): // quit on 'q'
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}
