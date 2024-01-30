mod editor;

use std::env;
use editor::Editor;

fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() > 1 {
        println!("open a file: {}", args[1]);

        let editor = Editor::new();
        editor.run()
    }
    else {
        Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no file specified"))
    }
}
