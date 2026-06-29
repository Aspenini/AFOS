// afos:api=1
// afos:id=afos.netinfo
// afos:capabilities=net:*

let status = net_status();
if status["link_up"] {
    print("link: up");
} else {
    print("link: down");
}
print("mac: " + status["mac"]);
let address = status["address"];
if address == () {
    print("address: (no DHCP lease)");
} else {
    print("address: " + address);
}
let gateway = status["gateway"];
if gateway != () {
    print("gateway: " + gateway);
}
0
