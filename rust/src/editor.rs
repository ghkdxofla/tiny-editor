use crossterm::event::{Event, KeyCode, KeyEvent, KeyModifiers};
use crossterm::{event, terminal};
use std::time::Duration;

pub struct Editor;

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
        Self
    }

    /**
     * if let is a syntax sugar for match that runs code when the value matches one pattern.
     * See https://doc.rust-kr.org/ch06-03-if-let.html
     */
    pub fn run(&self) -> std::io::Result<()> {
        self.enable_raw_mode();
        
        loop {
            if event::poll(Duration::from_millis(500))? {
                if let Event::Key(event) = event::read()? {
                    match event {
                        KeyEvent{
                            code: KeyCode::Char('q'),
                            modifiers: KeyModifiers::NONE,
                            ..
                        } => break,
                        _ => {

                        }
                    }
                }
            }
        }

        self.disable_raw_mode();
        Ok(())
    }

    fn enable_raw_mode(&self) {
        terminal::enable_raw_mode().expect("enable_raw_mode error");
    }

    fn disable_raw_mode(&self) {
        terminal::disable_raw_mode().expect("disable_raw_mode error");
    }
}
