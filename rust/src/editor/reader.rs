use crossterm::event::{Event, KeyEvent};
use crossterm::event;
use std::time::Duration;

pub struct Reader;

impl Reader {
    pub fn read_key(&self) -> std::io::Result<KeyEvent> {
        loop {
            if event::poll(Duration::from_millis(500))? {
                if let Event::Key(event) = event::read()? {
                    return Ok(event);
                }
            }
        }
    }
}
