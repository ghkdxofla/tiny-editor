mod editor;

use std::env;
use editor::Editor;

fn main() -> std::io::Result<()> {
    let editor = Editor::new();
    let args: Vec<String> = env::args().collect();

    if args.len() <= 1 {
        return Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no file specified"));
    }

    editor.enable_raw_mode();
    while editor.run()? {}
    Ok(())
}
