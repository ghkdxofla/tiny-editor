mod reader;
mod output;

use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use crossterm::terminal;
use reader::Reader;
use output::Output;

pub struct Editor {
    reader: Reader,
    output: Output,
}

/**
 * The Drop trait is used to run some code when a value goes out of scope.
 * See https://doc.rust-kr.org/ch15-03-drop.html
 */
impl Drop for Editor {
    fn drop(&mut self) {
        self.disable_raw_mode();
    }
}

impl Editor {
    pub fn new() -> Self {
        Self {
            reader: Reader,
            output: Output::new(),
        }
    }

    pub fn enable_raw_mode(&self) {
        terminal::enable_raw_mode().expect("enable_raw_mode error");
    }

    pub fn disable_raw_mode(&self) {
        terminal::disable_raw_mode().expect("disable_raw_mode error");
    }

    /**
     * if let is a syntax sugar for match that runs code when the value matches one pattern.
     * See https://doc.rust-kr.org/ch06-03-if-let.html
     */
    pub fn run(&self) -> std::io::Result<bool> {
        self.output.refresh_screen()?;
        self.process_keypress()
    }

    fn process_keypress(&self) -> std::io::Result<bool> {
        match self.reader.read_key()? {
            KeyEvent{
                code: KeyCode::Char('q'),
                modifiers: KeyModifiers::NONE,
                ..
            } => return Ok(false),
            _ => {}
        }
        Ok(true)
    }
}
