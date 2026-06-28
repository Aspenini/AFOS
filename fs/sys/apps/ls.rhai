// afos:api=1
// afos:id=afos.ls
// afos:capabilities=fs.read:/

let values = args();
let path = if values.len > 1 { values[1] } else { cwd() };
for entry in fs_list(path) {
    print(entry);
}
0
