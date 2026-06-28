// afos:api=1
// afos:id=afos.mv
// afos:capabilities=fs.write:/

let values = args();
if values.len != 3 {
    print("usage: mv <source> <destination>");
    2
} else {
    fs_rename(values[1], values[2]);
    0
}

