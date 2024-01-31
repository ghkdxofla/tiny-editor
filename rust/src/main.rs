mod editor;

use std::{env, intrinsics::likely};
use editor::Editor;

fn main() -> std::io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() > 1 {
        let editor = Editor::new();

        editor.disable_raw_mode();
        editor.enable_raw_mode();
        editor.run()
    }
    else {
        Err(std::io::Error::new(std::io::ErrorKind::NotFound, "no file specified"))
    }
}
