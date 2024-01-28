mod editor;

use std::env;
use editor::Editor;

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() > 1 {
        println!("open a file: {}", args[1]);

        let editor = Editor::new();
        editor.run();
    }
}
