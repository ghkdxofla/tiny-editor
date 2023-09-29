#include <termios.h>
#include <unistd.h>

void enableRawMode() {
    struct termios raw; // struct for storing terminal attributes

    tcgetattr(STDIN_FILENO, &raw); // get terminal attributes

    raw.c_lflag &= ~(ECHO); // disable echoing of input

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // set terminal attributes
}

int main() {
    enableRawMode();
    
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}
