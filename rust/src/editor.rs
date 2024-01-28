use crossterm::terminal;
use std::io;
use std::io::Read;

pub struct Editor;

/**
 * The Drop trait is used to run some code when a value goes out of scope.
 * See https://rinthel.github.io/rust-lang-book-ko/ch15-03-drop.html
 */
impl Drop for Editor {
    fn drop(&mut self) {
        self.disable_raw_mode();
    }
}

impl Editor {
    pub fn new() -> Self {
        Self
    }

    pub fn run(&self) {
        self.enable_raw_mode();

        let mut buf = [0; 1];
        while io::stdin().read(&mut buf).expect("read error") == 1 && buf != [b'q'] {
            println!("read: {:?}", buf);
        }

        self.disable_raw_mode();
    }

    fn enable_raw_mode(&self) {
        terminal::enable_raw_mode().expect("enable_raw_mode error");
    }

    fn disable_raw_mode(&self) {
        terminal::disable_raw_mode().expect("disable_raw_mode error");
    }
}
