// afos:api=1
// afos:id=afos.echo

let values = args();
if values.len > 1 {
    values.remove(0);
    print(values.join(" "));
} else {
    print("");
}
0

