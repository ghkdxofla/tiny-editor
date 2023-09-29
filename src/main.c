/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
    perror(s); // print error message. perror() looks at the global errno variable and prints a descriptive error message for it.
    exit(1); 
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
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
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr"); // get terminal attributes
    atexit(disableRawMode); // disable raw mode at exit

    struct termios raw = orig_termios; // copy terminal attributes

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

/*** init ***/

int main() {
    enableRawMode();
    
    while (1) {
        char c = '\0'; // initialize to null byte
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die ("read"); // read 1 byte from stdin into c. Cygwin returns -1 when there is no input available, so we have to check errno to make sure it’s not actually an error.
        /**
         * `iscntrl`
         * iscntrl() checks whether c is a control character.
         * ASCII codes 0–31 are all control characters, and 127 is also a control character. 
         * ASCII codes 32–126 are all printable.
        */
        if (iscntrl(c)) { // is control character
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break; // quit on 'q'
    }
    return 0;
}
