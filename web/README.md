# AFOS Web Emulator

This folder contains the web version of AFOS that runs in your browser using v86 (a WebAssembly x86 emulator).

## Building

To build for web, run from the project root:

**Windows (using WSL, Git Bash, or MSYS):**
```bash
make web
```

**Linux/macOS:**
```bash
make web
```

This will:
1. Build the AFOS ISO (if not already built)
2. Copy the ISO to the `web/` directory
3. Set up all necessary files for browser deployment

**Note for Windows users:** The `make` command requires a Unix-like environment. If you don't have Make installed:
- Use **WSL** (Windows Subsystem for Linux)
- Use **Git Bash** (comes with Git for Windows)
- Use **MSYS2** or **MinGW**
- Or manually build the ISO with `make iso` and copy `afos.iso` to the `web/` folder

## Running Locally

### Option 1: Python HTTP Server (Recommended)

**Windows:**
- Double-click `server.bat` (or run it from Command Prompt)
- Or run: `python -m http.server 8000`

**Linux/macOS:**
```bash
# Python 3
python3 -m http.server 8000
# Or use the server script
python3 server.py

# Python 2 (if Python 3 is not available)
python -m SimpleHTTPServer 8000
```

Then open http://localhost:8000 in your browser.

### Option 2: Node.js HTTP Server

If you have Node.js installed:

```bash
npx http-server -p 8000
```

### Option 3: PHP Built-in Server

```bash
php -S localhost:8000
```

### Option 4: Any Static File Server

You can use any static file server. The important thing is that it serves the files from this directory.

## Deployment

You can deploy this folder to any static web hosting service:

- **GitHub Pages**: Push to a `gh-pages` branch or use GitHub Actions
- **Netlify**: Drag and drop the `web/` folder to Netlify
- **Vercel**: Deploy the `web/` folder as a static site
- **Any web server**: Upload the contents of `web/` to your web server

## Features

- ✅ Runs AFOS in your browser via WebAssembly
- ✅ Full keyboard support
- ✅ Fullscreen mode
- ✅ Start/Stop/Reset controls
- ✅ Visual status indicators

## Notes

- The ISO file (`afos.iso`) needs to be served from the same origin due to browser security restrictions
- v86 uses CDN for BIOS files, so an internet connection is required (or you can download and host them locally)
- Network support is disabled by default (v86 network requires a relay server)
- The emulator allocates 128MB of RAM

## Troubleshooting

**OS doesn't boot:**
- Make sure `afos.iso` exists in the `web/` directory
- Check the browser console for errors
- Ensure the web server is serving files correctly (check that http://localhost:8000/afos.iso is accessible)

**Slow performance:**
- Close other browser tabs
- Use a modern browser (Chrome, Firefox, Edge, Safari)
- Try disabling browser extensions

**Keyboard input not working:**
- Click on the screen canvas to focus it
- Some keys may not map correctly - this is a limitation of browser keyboard handling

