#include <termios.h>
#include <unistd.h>

void enableRawMode() {
    struct termios raw; // struct for storing terminal attributes

    tcgetattr(STDIN_FILENO, &raw); // get terminal attributes

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
    */
    raw.c_lflag &= ~(ECHO); // disable echoing of input

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // set terminal attributes
}

int main() {
    enableRawMode();
    
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}
