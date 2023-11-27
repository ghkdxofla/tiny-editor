use termion::raw::IntoRawMode;
use std::io::{self, Write};

fn main() {
    let stdout = io::stdout().into_raw_mode().unwrap();

    write!(stdout, "{}", termion::clear::All).unwrap();
}
