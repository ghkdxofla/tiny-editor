/*** includes ***/

/**
 * `#define _DEFAULT_SOURCE`
 * This is a feature test macro that tells the compiler to include definitions for a POSIX source.
 * 
 * `#define _BSD_SOURCE`
 * This is a feature test macro that tells the compiler to include definitions for a BSD source.
 * 
 * `#define _GNU_SOURCE`
 * This is a feature test macro that tells the compiler to include definitions for a GNU source.
*/
#define _DEFAULT_SOURCE // for macOS
#define _BSD_SOURCE // for Linux
#define _GNU_SOURCE // for Linux

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define TINY_VERSION "0.0.1"
#define TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND k with 00011111

enum editorKey {
    ARROW_LEFT = 1000, // use large numbers to avoid conflict with char values
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow {
    int size;
    int rsize; // render size
    char *chars;
    char *render; // render string
} erow;

struct editorConfig {
    int cx, cy; // cursor x, y position
    int rx; // render x position
    int rowoff; // row offset
    int coloff; // column offset
    int screenrows;
    int screencols;
    int numrows; // number of rows
    erow *row; // pointer to array of erow structs. Dynamically allocated array of erow structs.
    char *filename; // filename
    char statusmsg[80]; // status message
    time_t statusmsg_time; // status message time
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

        /**
         * The Home key could be sent as <esc>[1~, <esc>[7~, <esc>[H, or <esc>OH.
         * Similarly, the End key could be sent as <esc>[4~, <esc>[8~, <esc>[F, or <esc>OF.
        */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; // read 1 byte from stdin into seq[2]
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY; // home
                        case '3': return DEL_KEY; // delete
                        case '4': return END_KEY; // end
                        case '5': return PAGE_UP; // page up
                        case '6': return PAGE_DOWN; // page down
                        case '7': return HOME_KEY; // home
                        case '8': return END_KEY; // end
                    }
                }
            } else {
            switch (seq[1]) {
                case 'A': return ARROW_UP; // up
                case 'B': return ARROW_DOWN; // down
                case 'C': return ARROW_RIGHT; // right
                case 'D': return ARROW_LEFT; // left
                case 'H': return HOME_KEY; // home
                case 'F': return END_KEY; // end
            }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY; // home
                case 'F': return END_KEY; // end
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

/*** row operations ***/

/**
 * `editorRowCxToRx()`
 * 
 * if it’s a tab we use rx % KILO_TAB_STOP to find out how many columns we are to the right of the last tab stop,
 * and then subtract that from KILO_TAB_STOP - 1 to find out how many columns we are to the left of the next tab stop.
*/
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP); // add number of spaces until next tab stop
        rx++;
    }

    return rx;
}

void editorUpdateRow(erow *row) {
    int j, tabs = 0;

    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (TAB_STOP - 1) + 1); // TAP_STOP spaces for each tab(1 space is already in the size of the row)

    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
        } else row->render[idx++] = row->chars[j];
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // allocate memory for new row
    
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0'; // null terminate string

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename); // strdup() returns a pointer to a new string which is a duplicate of the string s.

    FILE *fp = fopen(filename, "r"); // open file in read mode
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    /**
     * `getline()`
     * getline() reads an entire line from stream,
     * storing the address of the buffer containing the text into *lineptr.
    */
   while((linelen = getline(&line, &linecap, fp)) != -1) { // read line from file. returns -1 at the end of file
       while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--; // remove trailing newline characters
       editorAppendRow(line, linelen);
   }
    free(line);
    fclose(fp);
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

void editorScroll() {
    E.rx = 0;
    if(E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowoff) { // scroll up
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) { // scroll down
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) { // scroll left
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) { // scroll right
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) { // draw each row of the buffer to the screen
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
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
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0; // truncate row if it is too short
            if (len > E.screencols) len = E.screencols; // truncate row if it is too long
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }

        /**
         * `\x1b[K`
         * The escape sequence \x1b[K is a VT100 escape sequence that erases the current line the cursor is on.
         * 2 erases the whole line.
         * 1 erases from the start of the line to the cursor.
         * 0(default) erases from the cursor to the end of the line.
        */
        abAppend(ab, "\x1b[K", 3); // clear line (K; Erase In Line)
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4); // invert colors (7; Reverse Video)
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows); // current row and total number of rows
    if (len > E.screencols) len = E.screencols; // truncate status message if it is too long
    abAppend(ab, status, len);

    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // reset colors (m; Turn Off Character Attributes)
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3); // clear line (K; Erase In Line)
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols; // truncate message if it is too long
    if (msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab, E.statusmsg, msglen); // display message for 5 seconds
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
    editorScroll();

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
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];

    /**
     * `snprintf()`
     * snprintf() writes to the character string str.
     * snprintf() appends the terminating null byte ('\0') to the output string.
     * save E.cy + 1 and E.cx + 1 to buf with format "\x1b[%d;%dH" and length of buf
    */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); // reposition cursor
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show cursor(h; Set Mode)

    write(STDOUT_FILENO, ab.b, ab.len); // write abuf to stdout
    abFree(&ab);
}

/**
 * `editorSetStatusMessage()`
 * sets the status message to be displayed in the status bar.
 * 
 * The ... argument makes editorSetStatusMessage() a variadic function,
 * meaning it can take any number of arguments.
 * C’s way of dealing with these arguments is by having you call va_start() and va_end() on a value of type va_list.
 * The last argument before the ... (in this case, fmt) must be passed to va_start(),
 * so that the address of the next arguments is known.
 * Then, between the va_start() and va_end() calls,
 * you would call va_arg() and pass it the type of the next argument (which you usually get from the given format string)
 * and it would return the value of that argument.
 * In this case, we pass fmt and ap to vsnprintf()
 * and it takes care of reading the format string and calling va_arg() to get each argument.
*/
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap; // va_list is a type to hold information about variable arguments
    va_start(ap, fmt); // initialize ap to start after fmt
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap); // write formatted output to E.statusmsg
    va_end(ap); // clean up the va_list
    E.statusmsg_time = time(NULL); // set status message time to current time
}

/*** input ***/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; // get current row (if it exists)
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) E.cx--;
            else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) E.cx++;
            else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; // get current row (if it exists)
    int rowlen = row ? row->size : 0; // get length of current row
    if (E.cx > rowlen) {
        E.cx = rowlen;
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

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                int times = E.screenrows;
                while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
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
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0'; // initialize status message to empty string
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2; // make room for status bar and message bar
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}
