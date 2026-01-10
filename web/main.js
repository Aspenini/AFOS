// AFOS Web Emulator
let emulator = null;
let isRunning = false;
const ISO_PATH = './afos.iso';

const startBtn = document.getElementById('start-btn');
const screenContainer = document.getElementById('screen-container');
const statusEl = document.getElementById('status');
const logsEl = document.getElementById('logs');

function log(message) {
    if (!logsEl) return;
    const time = new Date().toLocaleTimeString();
    logsEl.innerHTML += `[${time}] ${message}<br>`;
    logsEl.scrollTop = logsEl.scrollHeight;
    console.log(message);
}

function updateStatus(text, className = '') {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.className = 'status-text ' + className;
    log(text);
}

// Wait for v86 to load
function waitForV86(callback, maxAttempts = 100) {
    let attempts = 0;
    updateStatus('Loading v86 library...', 'loading');
    const check = setInterval(() => {
        attempts++;
        const V86 = window.V86Starter || window.V86;
        if (typeof V86 !== 'undefined') {
            clearInterval(check);
            window.V86Starter = V86;
            updateStatus('v86 library loaded', 'running');
            log('v86 library loaded successfully');
            callback();
        } else if (attempts >= maxAttempts) {
            clearInterval(check);
            updateStatus('Error: v86 library failed to load', 'error');
            log('ERROR: v86 library failed to load after ' + maxAttempts + ' attempts');
            if (startBtn) {
                startBtn.disabled = false;
                startBtn.textContent = 'ERROR: REFRESH PAGE';
                startBtn.style.background = '#ff0000';
                startBtn.style.color = '#fff';
            }
        }
    }, 100);
}

function startEmulator() {
    if (isRunning) {
        return;
    }
    
    if (typeof window.V86Starter === 'undefined') {
        updateStatus('Waiting for v86 library...', 'loading');
        waitForV86(startEmulator);
        return;
    }
    
    isRunning = true;
    if (startBtn) {
        startBtn.disabled = true;
        startBtn.textContent = 'STARTING...';
    }
    updateStatus('Initializing emulator...', 'loading');
    
    try {
        log('Creating v86 emulator instance...');
        emulator = new window.V86Starter({
            screen_container: screenContainer,
            memory_size: 128 * 1024 * 1024,
            vga_memory_size: 8 * 1024 * 1024,
            cdrom: {
                async: true,
                url: ISO_PATH,
            },
            autostart: true,
            bios: {
                url: "https://cdn.jsdelivr.net/npm/@copy/v86@0.9.2/bios/seabios.bin"
            },
            vga_bios: {
                url: "https://cdn.jsdelivr.net/npm/@copy/v86@0.9.2/bios/vgabios.bin"
            },
            boot_order: 0x3,
            keyboard: {
                keyboard_layout: (window.V86Starter && window.V86Starter.KeyboardLayout) ? window.V86Starter.KeyboardLayout.US : 'us',
            },
        });
        
        log('Emulator instance created');
        updateStatus('Loading BIOS...', 'loading');
        
        emulator.add_listener("emulator-ready", function() {
            log('Emulator ready');
            updateStatus('Emulator ready, booting OS...', 'running');
        });
        
        emulator.add_listener("download-progress", function(loaded, total) {
            const percent = Math.round((loaded / total) * 100);
            updateStatus(`Loading: ${percent}%`, 'loading');
            log(`Download progress: ${percent}% (${loaded}/${total} bytes)`);
        });
        
        emulator.add_listener("emulator-started", function() {
            log('Emulator started - OS should boot now');
            updateStatus('OS running', 'running');
            if (startBtn) {
                startBtn.style.display = 'none';
            }
        });
        
        emulator.add_listener("screen-put-char", function(ch) {
            // OS is outputting characters
        });
        
        emulator.add_listener("error", function(error) {
            console.error('Emulator error:', error);
            log('ERROR: ' + (error.message || error.toString() || 'Unknown error'));
            updateStatus('Error: ' + (error.message || 'Unknown error'), 'error');
            if (startBtn) {
                startBtn.disabled = false;
                startBtn.textContent = 'ERROR: CLICK TO RETRY';
                startBtn.style.background = '#ff0000';
                startBtn.style.color = '#fff';
            }
            isRunning = false;
        });
        
        // Check if ISO file exists
        fetch(ISO_PATH, { method: 'HEAD' })
            .then(response => {
                if (response.ok) {
                    log('ISO file found: ' + ISO_PATH);
                } else {
                    log('WARNING: ISO file not found or not accessible: ' + ISO_PATH);
                    updateStatus('Warning: ISO not found', 'error');
                }
            })
            .catch(error => {
                log('ERROR: Could not check ISO file: ' + error);
                updateStatus('Error: Cannot access ISO', 'error');
            });
        
    } catch (error) {
        console.error('Failed to start emulator:', error);
        log('EXCEPTION: ' + error.toString());
        updateStatus('Error: ' + error.message, 'error');
        if (startBtn) {
            startBtn.disabled = false;
            startBtn.textContent = 'ERROR: CLICK TO RETRY';
            startBtn.style.background = '#ff0000';
            startBtn.style.color = '#fff';
        }
        isRunning = false;
    }
}

// Initialize when v86 is loaded
waitForV86(() => {
    if (startBtn) {
        startBtn.textContent = 'START';
        startBtn.disabled = false;
    }
    updateStatus('Ready to start', 'running');
});

if (startBtn) {
    startBtn.addEventListener('click', startEmulator);
}

// Focus screen container for keyboard input
if (screenContainer) {
    screenContainer.addEventListener('click', function() {
        const canvas = screenContainer.querySelector('canvas');
        if (canvas) {
            canvas.focus();
            log('Canvas focused - keyboard input ready');
        }
    });
}

// Initial log after DOM is ready
setTimeout(() => {
    log('AFOS Web Emulator initialized');
    updateStatus('Initializing...', 'loading');
}, 100);
