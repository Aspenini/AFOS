// afos:api=1
// afos:id=afos.help
// afos:capabilities=fs.read:/sys/apps,fs.read:/apps

print("AFOS commands");
print("  cd [path]       change directory");
print("  clear           clear the terminal");
print("  passwd          configure the master password");
print("  run <path> ...  run an application by path");
print("  exit [status]   leave AFOS");
print("");
print("Bundled applications:");
for entry in fs_list("/sys/apps") {
    if entry.ends_with(".rhai") {
        print("  " + entry.sub_string(0, entry.len - 5));
    }
}
let installed = fs_list("/apps");
if installed.len > 0 {
    print("");
    print("Installed applications:");
    for entry in installed {
        print("  " + entry);
    }
}
0
