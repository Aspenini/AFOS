// afos:api=1
// afos:id=afos.sysinfo
// afos:capabilities=system.info

let info = system_info();
print(info["name"] + " " + info["version"]);
print("platform: " + info["platform"]);
print("architecture: " + info["architecture"]);
0

