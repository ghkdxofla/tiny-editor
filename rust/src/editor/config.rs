pub struct Config {
  cx: int,
  cy: int,
  rx: int,
  rowoff: int,
  coloff: int,
  screenrows: int,
  screencols: int,
  numrows: int,
  rows: [Erow],
  dirty: int,
  filename: String,
  statusmsg: String,
  statuemsg_time: Time,
  syntax: Syntax,
}