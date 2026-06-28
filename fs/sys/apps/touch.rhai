// afos:api=1
// afos:id=afos.touch
// afos:capabilities=fs.write:/

let values = args();
if values.len < 2 {
    print("usage: touch <file>...");
    2
} else {
    for index in 1..values.len {
        fs_write(values[index], "");
    }
    0
}
