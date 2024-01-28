use crossterm::terminal;
use std::env;
use std::io;
use std::io::Read;

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        // TODO: Add editor to open file
        terminal::enable_raw_mode().expect("enable_raw_mode error");
        println!("open a file: {}", args[1]);

        let mut buf = [0; 1];
        while io::stdin().read(&mut buf).expect("read error") == 1 && buf != [b'q'] {
            println!("read: {:?}", buf);
        }
    }
}
