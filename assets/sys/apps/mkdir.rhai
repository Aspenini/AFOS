// afos:api=1
// afos:id=afos.mkdir
// afos:capabilities=fs.write:/

let values = args();
if values.len < 2 {
    print("usage: mkdir <directory>...");
    2
} else {
    for index in 1..values.len {
        fs_mkdir(values[index]);
    }
    0
}

