// afos:api=1
// afos:id=afos.cp
// afos:capabilities=fs.read:/,fs.write:/

let values = args();
if values.len != 3 {
    print("usage: cp <source> <destination>");
    2
} else {
    fs_write(values[2], fs_read(values[1]));
    0
}
