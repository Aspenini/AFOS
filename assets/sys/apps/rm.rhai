// afos:api=1
// afos:id=afos.rm
// afos:capabilities=fs.write:/

let values = args();
if values.len < 2 {
    print("usage: rm <path>...");
    2
} else {
    for index in 1..values.len {
        fs_remove(values[index]);
    }
    0
}

