use crossterm::execute;
use crossterm::terminal::{Clear, ClearType};
use std::io::stdout;

pub struct Output;

impl Output {
    pub fn new() -> Self {
        Self
    }

    pub fn clear_screen() -> std::io::Result<()> {
        execute!(stdout(), Clear(ClearType::All))
    }

    pub fn refresh_screen(&self) -> std::io::Result<()> {
        Self::clear_screen()
    }
}
