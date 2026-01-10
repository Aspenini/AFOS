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

// Wait for v86 to load - check multiple possible names
function waitForV86(callback, maxAttempts = 150) {
    let attempts = 0;
    updateStatus('Loading v86 library...', 'loading');
    log('Checking for v86 library...');
    
    const check = setInterval(() => {
        attempts++;
        
        // Check multiple possible global variable names
        let V86 = null;
        if (typeof window.V86Starter !== 'undefined') {
            V86 = window.V86Starter;
            log('Found V86Starter');
        } else if (typeof window.V86 !== 'undefined') {
            V86 = window.V86;
            log('Found V86');
        } else if (typeof window.v86 !== 'undefined') {
            V86 = window.v86;
            log('Found v86 (lowercase)');
        } else if (typeof V86Starter !== 'undefined') {
            V86 = V86Starter;
            log('Found V86Starter (no window prefix)');
        }
        
        if (V86 && typeof V86 === 'function') {
            clearInterval(check);
            window.V86Starter = V86; // Normalize to V86Starter
            updateStatus('v86 library loaded', 'running');
            log('v86 library loaded successfully - ready to use');
            callback();
        } else if (attempts >= maxAttempts) {
            clearInterval(check);
            updateStatus('Error: v86 library failed to load', 'error');
            log('ERROR: v86 library failed to load after ' + maxAttempts + ' attempts');
            log('Available globals: ' + Object.keys(window).filter(k => k.toLowerCase().includes('v86')).join(', ') || 'none');
            if (startBtn) {
                startBtn.disabled = false;
                startBtn.textContent = 'ERROR: REFRESH PAGE';
                startBtn.style.background = '#ff0000';
                startBtn.style.color = '#fff';
            }
        } else if (attempts % 10 === 0) {
            log('Still waiting for v86 library... (attempt ' + attempts + '/' + maxAttempts + ')');
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
                url: "https://cdn.jsdelivr.net/gh/copy/v86@master/bios/seabios.bin"
            },
            vga_bios: {
                url: "https://cdn.jsdelivr.net/gh/copy/v86@master/bios/vgabios.bin"
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

// Initialize when DOM and scripts are ready
function init() {
    log('AFOS Web Emulator initialized');
    updateStatus('Initializing...', 'loading');
    
    // Wait for v86 library to load
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
}

// Wait for page to fully load before initializing
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        // Give v86 script time to load
        setTimeout(init, 500);
    });
} else {
    // Page already loaded, wait a bit for v86 script
    setTimeout(init, 500);
}
