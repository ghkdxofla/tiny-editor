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
#include <fcntl.h>
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
#define QUIT_TIMES 2

#define CTRL_KEY(k) ((k) & 0x1f) // bitwise-AND k with 00011111

enum editorKey {
    BACKSPACE = 127, // use large numbers to avoid conflict with char values (127; ASCII)
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

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT, // multi line comment
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0) // 00000001
#define HL_HIGHLIGHT_STRINGS (1<<1) // 00000010

/*** data ***/

struct editorSyntax {
    char *filetype; // file extension
    char **filematch; // filename
    char **keywords; // keywords
    char *singleline_comment_start; // single line comment start
    char *multiline_comment_start; // multi line comment start
    char *multiline_comment_end; // multi line comment end
    int flags; // flags
};

typedef struct erow {
    int idx; // index
    int size;
    int rsize; // render size
    char *chars;
    char *render; // render string
    unsigned char *hl; // highlight
    int hl_open_comment; // highlight open comment
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
    int dirty; // dirty flag
    char *filename; // filename
    char statusmsg[80]; // status message
    time_t statusmsg_time; // status message time
    struct editorSyntax *syntax; // pointer to editorSyntax struct
    struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL }; // NULL is used to mark the end of the array
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case",

    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", // single line comment start
        "/*", // multi line comment start
        "*/", // multi line comment end
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0])) // number of elements in HLDB

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

/*** syntax highlighting ***/

int is_seperator(int c) {
    /**
     * '\0' is the null character.
     * It can count the null byte at the end of each line as a separator.
    */
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL; // strchr() returns a pointer to the first occurrence of c in the string
}

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->rsize); // allocate memory for highlight array
    memset(row->hl, HL_NORMAL, row->rsize); // memset() fills the first n bytes of the memory area pointed to by row->hl with the constant byte HL_NORMAL

    if (E.syntax == NULL) return; // if no syntax, return

    char **keywords = E.syntax->keywords; // keywords

    char *scs = E.syntax->singleline_comment_start; // single line comment start
    char *mcs = E.syntax->multiline_comment_start; // multi line comment start
    char *mce = E.syntax->multiline_comment_end; // multi line comment end

    int scs_len = scs ? strlen(scs) : 0; // single line comment start length
    int mcs_len = mcs ? strlen(mcs) : 0; // multi line comment start length
    int mce_len = mce ? strlen(mce) : 0; // multi line comment end length
    
    int prev_sep = 1; // previous separator; 1 if previous character is a separator, 0 otherwise. we consider the beginning of the line to be a separator. (Otherwise numbers at the very beginning of the line wouldn’t be highlighted.)
    int in_string = 0; // if in_string > 0 we are inside a string, 0 otherwise
    int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment); // if in_comment > 0, we are inside a multi line comment, 0 otherwise (if previous row is a multi line comment

    int i = 0;
    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL; // previous highlight

        if (scs_len && !in_string && !in_comment) { // if single line comment start exists and we are not in a string
            /**
             * `strncmp()`
             * strncmp() compares the first n bytes of s1 and s2.
             * It returns an integer less than, equal to, or greater than zero if s1 is found, respectively, to be less than, to match, or be greater than s2.
             * A positive value means that s1 would be after s2 in a dictionary.
             * A negative value means that s1 would be before s2 in a dictionary.
            */
            if (!strncmp(&row->render[i], scs, scs_len)) { // strncmp() compares the first n bytes of row->render[i] and scs
                memset(&row->hl[i], HL_COMMENT, row->rsize - i); // highlight comment
                break;
            }
        }

        if (mcs_len && mce_len && !in_string) {
            if (in_comment) { // if in_comment > 0, we are inside a multi line comment
                row->hl[i] = HL_MLCOMMENT; // highlight multi line comment
                if (!strncmp(&row->render[i], mce, mce_len)) { // strncmp() compares the first n bytes of row->render[i] and mce
                    memset(&row->hl[i], HL_MLCOMMENT, mce_len); // highlight multi line comment
                    i += mce_len;
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                } else {
                    i++;
                    continue;
                }
            } else if (!strncmp(&row->render[i], mcs, mcs_len)) { // strncmp() compares the first n bytes of row->render[i] and mcs
                memset(&row->hl[i], HL_MLCOMMENT, mcs_len); // highlight multi line comment
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }
        
        if (E.syntax->flags * HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING; // highlight string
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING; // highlight escape character
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0; // if c is the same as in_string, set in_string to 0
                i++;
                prev_sep = 1;
                continue;
            } else {
                if (c == '"' || c == '\'') { // if c is a double quote or single quote
                    in_string = c; // set in_string to c
                    row->hl[i] = HL_STRING; // highlight string
                    i++;
                    continue;
                }
            }
        }
        
        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
                (c == '.' && prev_hl == HL_NUMBER)) { // if c is a digit and previous character is a separator or previous character is a number, or if c is a decimal point and previous character is a number
                row->hl[i] = HL_NUMBER; // highlight numbers
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) { // if previous character is a separator
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = strlen(keywords[j]); // keyword length
                int kw2 = keywords[j][klen - 1] == '|'; // 1 if keyword ends with '|', 0 otherwise
                if (kw2) klen--; // if keyword ends with '|', decrement keyword length

                if (!strncmp(&row->render[i], keywords[j], klen) &&
                    is_seperator(row->render[i + klen])) { // if keyword matches and next character is a separator
                    memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen); // highlight keyword
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) { // if keyword matches
                prev_sep = 0;
                continue;
            }
        }

        prev_sep = is_seperator(c);
        i++;
    }

    int changed = (row->hl_open_comment != in_comment); // 1 if row->hl_open_comment != in_comment, 0 otherwise
    row->hl_open_comment = in_comment; // set row->hl_open_comment to in_comment
    if (changed && row->idx + 1 < E.numrows) editorUpdateSyntax(&E.row[row->idx + 1]); // if changed and row->idx + 1 < E.numrows, update syntax of next row
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT: 
        case HL_MLCOMMENT: return 36; // cyan
        case HL_KEYWORD1: return 33; // yellow
        case HL_KEYWORD2: return 32; // green
        case HL_STRING: return 35; // magenta
        case HL_NUMBER: return 31; // red
        case HL_MATCH: return 34; // blue
        default: return 37; // white
    }
}

void editorSelectSyntaxHighlight() {
    E.syntax = NULL;
    if (E.filename == NULL) return; // if no filename, return

    char *ext = strrchr(E.filename, '.'); // strrchr() returns a pointer to the last occurrence of the character '.' in the string E.filename. If no match is found, then NULL is returned.

    for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
        struct editorSyntax *s = &HLDB[j];
        unsigned int i = 0;
        while (s->filematch[i]) {
            int is_ext = (s->filematch[i][0] == '.');
            /**
             * `strcmp()`
             * strcmp() compares the two strings s1 and s2.
             * It returns an integer less than, equal to, or greater than zero if s1 is found, respectively, to be less than, to match, or be greater than s2.
             * A positive value means that s1 would be after s2 in a dictionary.
             * A negative value means that s1 would be before s2 in a dictionary.
            */
            if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || // strcmp() returns 0 if the strings are equal. In C, 0 is false, and any other integer is true.
                (!is_ext && strstr(E.filename, s->filematch[i]))) { // strstr() returns a pointer to the first occurrence of s->filematch[i] in E.filename
                E.syntax = s;

                int filerow;
                for (filerow = 0; filerow < E.numrows; filerow++) {
                    editorUpdateSyntax(&E.row[filerow]);
                }

                return;
            }
            i++;
        }
    }
}

/*** row operations ***/

/**
 * `editorRowCxToRx()`
 * 
 * if it’s a tab we use rx % KILO_TAB_STOP to find out how many columns we are to the right of the last tab stop,
 * and then subtract that from KILO_TAB_STOP - 1 to find out how many columns we are to the left of the next tab stop.
 * 
 * Why rx % TAB_STOP?
 * 
 * In TAB_STOP = 4,
 *     ↓   ↓
 * a►--b►--c
 * dd►-ee►-ff
 * ggg►hhh►iii
 * 
 * a   b   c
 * dd  ee  ff
 * ggg hhh iii
 * https://vi.stackexchange.com/questions/3973/why-are-tab-characters-variable-width
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

/**
 * `editorRowRxToCx()`
 * 
 * if it’s a tab we use cur_rx % KILO_TAB_STOP to find out how many columns we are to the right of the last tab stop,
 * and then subtract that from KILO_TAB_STOP - 1 to find out how many columns we are to the left of the next tab stop.
*/
int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP); // add number of spaces until next tab stop
        cur_rx++;

        if (cur_rx > rx) return cx; // return index of character at cx
    }

    return cx;
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

    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return; // if at is out of bounds, return

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // allocate memory for new row
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at)); // move rows after at to the right by 1 (memmove() is like memcpy() but it works even if the memory regions overlap
    for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++; // increment idx of rows after at by 1

    E.row[at].idx = at;

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0'; // null terminate string

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_open_comment = 0;
    editorUpdateRow(&E.row[at]);

    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return; // if at is out of bounds, return
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1)); // move rows after at to the left by 1
    for (int j = at; j < E.numrows; j++) E.row[j].idx--; // decrement idx of rows after at by 1
    E.numrows--;
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size; // if at is out of bounds, set it to the end of the row

    row->chars = realloc(row->chars, row->size + 2); // allocate memory for new char. add 2 because we also have to make room for the null byte
    /**
     * `memmove()`
     * memmove() copies n bytes from memory area src to memory area dest.
     * The memory areas may overlap: copying takes place as though the bytes in src are first copied into a temporary array that does not overlap src or dest, 
     * and the bytes are then copied from the temporary array to dest.
    */
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1); // move chars after at to the right by 1
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1); // allocate memory for new string including null byte
    memcpy(&row->chars[row->size], s, len); // copy string s to end of row
    row->size += len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return; // if at is out of bounds, return
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at); // move chars after at to the left by 1
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
    if (E.cy == E.numrows) { // if cursor is at the end of the file
        editorInsertRow(E.numrows, "", 0); // append empty row
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c); // insert char at cursor position
    E.cx++;
}

void editorInsertNewLine() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0); // insert empty row at cursor position
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx); // insert new row at cursor position
        row = &E.row[E.cy]; // update row pointer. editorInsertRow() calls realloc(), which might move memory around on us and invalidate the pointer (yikes)
        row->size = E.cx;
        row->chars[row->size] = '\0'; // null terminate string
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    if (E.cy == E.numrows) return; // if cursor is at the end of the file
    if (E.cx == 0 & E.cy == 0) return; // if cursor is at the beginning of the file

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1); // delete char to the left of the cursor
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size; // move cursor to the end of the previous line
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size); // append current line to previous line
        editorDelRow(E.cy); // delete current line
        E.cy--;
    }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++) totlen += E.row[j].size + 1; // add length of each row + 1 for newline character
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf; // pointer to buf
    for (j = 0; j < E.numrows; j++) { // copy each row to buf
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size; // move pointer to end of row
        *p = '\n'; // add newline character
        p++;
    }

    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename); // strdup() returns a pointer to a new string which is a duplicate of the string s.

    editorSelectSyntaxHighlight();

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
       editorInsertRow(E.numrows, line, linelen);
   }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL); // prompt user for filename
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
        editorSelectSyntaxHighlight();
    }

    int len;
    char *buf = editorRowsToString(&len);

    /**
     * `O_RDWR`
     * Open the file so that it can be read from and written to.
     * 
     * `O_CREAT`
     * If the file does not exist it will be created.
     * 
     * `O_TRUNC`
     * If the file already exists and is a regular file and the access mode allows writing (i.e., is O_RDWR or O_WRONLY) it will be truncated to length 0.
     * 
     * `0644`
     * The mode specifies both the permissions to use if a new file is created and the permissions for the file if it already exists.
     * It gives the owner of the file permission to read and write the file,
     * and everyone else only gets permission to read the file.
    */
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644); // open file in read/write mode. create file if it doesn't exist
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) { // truncate file to a specified length
            /**
             * `ftruncate()`
             * If the file previously was larger than this size, the extra data is lost.
             * If the file previously was shorter, it is extended, and the extended part reads as null bytes ('\0').
            */
            if (write(fd, buf, len) == len) { // write buf to file
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno)); // strerror() returns a pointer to a string that describes the error code passed in the argument errnum
}

/*** find ***/

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1; // 1 for forward, -1 for backward

    static int saved_hl_line;
    static char *saved_hl = NULL;

    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize); // restore saved highlight
        free(saved_hl);
        saved_hl = NULL;
    }

    if (key == '\r' || key == '\x1b') { // if enter or escape is pressed
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for (i = 0; i < E.numrows; i++) { // search each row
        current += direction;
        if (current == -1) current = E.numrows - 1; // wrap around to bottom of file
        else if (current == E.numrows) current = 0; // wrap around to top of file

        erow *row = &E.row[current];
        char *match = strstr(row->render, query); // strstr() returns a pointer to the first occurrence of query in row->render, or NULL if no match is found
        if (match) {
            last_match = current;
            E.cy = current;
            /**
             * match - row->render?
             * match도 pointer고 row->render도 pointer라서 빼면 그 사이의 거리가 나온다.
             * 그 거리가 cursor position이다.
            */
            E.cx = editorRowRxToCx(row, match - row->render); // set cursor position to beginning of match
            E.rowoff = E.numrows; // scroll to bottom of file

            saved_hl_line = current;
            saved_hl = malloc(row->rsize); // allocate memory for saved highlight
            memcpy(saved_hl, row->hl, row->rsize); // save current highlight

            memset(&row->hl[match - row->render], HL_MATCH, strlen(query)); // highlight match
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
    
    if (query) {
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
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
            char *c = &E.row[filerow].render[E.coloff];
            unsigned char *hl = &E.row[filerow].hl[E.coloff];
            int current_color = -1;
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    /**
                     * Why '@'?
                     * In ASCII, the capital letters of the alphabet come after the @ character
                    */
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?'; // replace non-printable characters with '?'
                    abAppend(ab, "\x1b[7m", 4); // invert colors (7; Reverse Video)
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3); // reset colors (m; Turn Off Character Attributes)
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color); // set color
                        abAppend(ab, buf, clen);
                    }
                } else if (hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        abAppend(ab, "\x1b[39m", 5); // reset color
                        current_color = -1;
                    }
                    abAppend(ab, &c[j], 1);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color); // set color
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5); // reset color
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
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
        E.filename ? E.filename : "[No Name]", E.numrows,
        E.dirty ? "(modified)" : ""
    );
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", 
        E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows); // current row and total number of rows and filetype
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

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0'; // initialize buf to empty string

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) { // delete char on 'delete', 'ctrl-h', or 'backspace'
            if (buflen != 0) buf[--buflen] = '\0'; // remove last char from buf
        } else if (c == '\x1b') { // escape key
            editorSetStatusMessage(""); // clear status message
            if (callback) callback(buf, c); // call callback function
            free(buf);
            return NULL;
        } else if (c == '\r') { // enter key
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c); // call callback function
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) { // if c is not a control character and is less than 128
            if (buflen == bufsize - 1) {
                bufsize *= 2; // double buffer size
                buf = realloc(buf, bufsize); // reallocate memory for new buffer size
            }
            buf[buflen++] = c;
            buf[buflen] = '\0'; // null terminate string
        }

        if (callback) callback(buf, c); // call callback function
    }
}

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
    static int quit_times = QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewLine();
            break;

        case CTRL_KEY('q'): // quit on 'q'
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'): // save on 'ctrl-s'
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
            break;

        case CTRL_KEY('f'): // find on 'ctrl-f'
            editorFind();
            break;

        /**
         * We also handle the Ctrl-H key combination, 
         * which sends the control code 8, which is originally what the Backspace character would send back in the day. 
         * If you look at the ASCII table, you’ll see that ASCII code 8 is named BS for "backspace"
        */
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT); // move cursor right on 'delete'
            editorDelChar();
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

        /**
         * ignore the Escape key because there are many key escape sequences that we aren’t handling (such as the F1–F12 keys), 
         * and the way we wrote editorReadKey(), pressing those keys will be equivalent to pressing the Escape key. 
        */
        case CTRL_KEY('l'): // refresh screen on 'ctrl-l'
        case '\x1b': // escape key
            break;
        
        default:
            editorInsertChar(c);
            break;
    }

    quit_times = QUIT_TIMES;
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
    E.dirty = 0; // initialize dirty flag to false
    E.filename = NULL;
    E.statusmsg[0] = '\0'; // initialize status message to empty string
    E.statusmsg_time = 0;
    E.syntax = NULL; // initialize syntax highlighting to NULL. There is no filetype for the current file

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2; // make room for status bar and message bar
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    return 0;
}
