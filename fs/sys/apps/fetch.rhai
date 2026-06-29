// afos:api=1
// afos:id=afos.fetch
// afos:capabilities=net:*

let argv = args();
if argv.len() < 2 {
    print("usage: fetch <host> [path]");
    return 1;
}
let host = argv[1];
let path = if argv.len() >= 3 { argv[2] } else { "/" };

let socket = net_connect(host, 80);
let request = "GET " + path + " HTTP/1.0\r\n" +
    "Host: " + host + "\r\n" +
    "Connection: close\r\n\r\n";
net_send(socket, request);

let total = 0;
loop {
    let chunk = net_recv(socket, 1024);
    if chunk == "" {
        break;
    }
    print_raw(chunk);
    total += chunk.len();
    if total > 8192 {
        break;
    }
}
net_close(socket);
print("");
0
