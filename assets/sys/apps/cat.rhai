// afos:api=1
// afos:id=afos.cat
// afos:capabilities=fs.read:/

let values = args();
if values.len < 2 {
    print("usage: cat <file>");
    2
} else {
    print_raw(fs_read(values[1]));
    0
}

