// AFOS Web Emulator - Using v86
let emulator = null;
let isRunning = false;

// Get DOM elements
const startBtn = document.getElementById('start-btn');
const stopBtn = document.getElementById('stop-btn');
const resetBtn = document.getElementById('reset-btn');
const fullscreenBtn = document.getElementById('fullscreen-btn');
const screenContainer = document.getElementById('screen-container');
const statusText = document.querySelector('.status-text');
const statusIndicator = document.querySelector('.status-indicator');
const container = document.querySelector('.container');

// Check if ISO file exists
const ISO_PATH = './afos.iso';

// Initialize emulator
function startEmulator() {
    if (isRunning) {
        return;
    }
    
    // Check if v86 is loaded
    if (typeof V86Starter === 'undefined') {
        updateStatus('Error: v86 library not loaded. Please refresh the page.', 'stopped');
        return;
    }
    
    updateStatus('Starting emulator...', 'running');
    startBtn.disabled = true;
    
    // Remove existing canvas if it exists (v86 will create its own)
    const existingCanvas = screenContainer.querySelector('canvas');
    if (existingCanvas) {
        existingCanvas.remove();
    }
    
    try {
        // Configure v86 emulator
        emulator = new V86Starter({
            screen_container: screenContainer,
            memory_size: 128 * 1024 * 1024, // 128MB
            vga_memory_size: 8 * 1024 * 1024,
            cdrom: {
                async: true,
                url: ISO_PATH,
            },
            autostart: true,
            bios: {
                url: "https://cdn.jsdelivr.net/gh/copy/v86@latest/bios/seabios.bin"
            },
            vga_bios: {
                url: "https://cdn.jsdelivr.net/gh/copy/v86@latest/bios/vgabios.bin"
            },
            boot_order: 0x3, // Boot from CD-ROM (0x80 = hard disk, 0x1 = floppy, 0x3 = CD-ROM)
            network_relay_url: "<UNUSED>",
            // Disable network for now (v86 network requires relay server)
            // network_relay_url: "wss://relay.widgetry.org/",
            keyboard: {
                keyboard_layout: V86Starter.KeyboardLayout.US,
            },
        });
        
        // Handle emulator events
        emulator.add_listener("emulator-ready", function() {
            updateStatus('Emulator ready, booting OS...', 'running');
        });
        
        emulator.add_listener("screen-put-char", function(ch) {
            // Character displayed
        });
        
        emulator.add_listener("emulator-started", function() {
            isRunning = true;
            updateStatus('OS is running', 'running');
            startBtn.disabled = true;
            stopBtn.disabled = false;
            resetBtn.disabled = false;
        });
        
        // Error handling
        emulator.add_listener("download-progress", function(loaded, total) {
            const percent = Math.round((loaded / total) * 100);
            updateStatus(`Loading BIOS: ${percent}%`, 'running');
        });
        
        emulator.add_listener("error", function(error) {
            console.error('Emulator error:', error);
            updateStatus('Error: ' + (error.message || 'Unknown error'), 'stopped');
            isRunning = false;
            startBtn.disabled = false;
            stopBtn.disabled = true;
            resetBtn.disabled = true;
        });
        
    } catch (error) {
        console.error('Failed to start emulator:', error);
        updateStatus('Failed to start: ' + error.message, 'stopped');
        startBtn.disabled = false;
        isRunning = false;
    }
}

function stopEmulator() {
    if (!emulator || !isRunning) {
        return;
    }
    
    try {
        emulator.stop();
        emulator = null;
        isRunning = false;
        updateStatus('Emulator stopped', 'stopped');
        startBtn.disabled = false;
        stopBtn.disabled = true;
        resetBtn.disabled = true;
        
        // Clear screen container
        screenContainer.innerHTML = '<canvas id="screen"></canvas>';
    } catch (error) {
        console.error('Error stopping emulator:', error);
    }
}

function resetEmulator() {
    if (!emulator || !isRunning) {
        return;
    }
    
    stopEmulator();
    // Wait a bit before restarting
    setTimeout(() => {
        startEmulator();
    }, 500);
}

function toggleFullscreen() {
    if (!document.fullscreenElement) {
        container.classList.add('fullscreen');
        container.requestFullscreen().catch(err => {
            console.error('Error entering fullscreen:', err);
            container.classList.remove('fullscreen');
        });
        fullscreenBtn.textContent = 'Exit Fullscreen';
    } else {
        document.exitFullscreen();
        container.classList.remove('fullscreen');
        fullscreenBtn.textContent = 'Fullscreen';
    }
}

function updateStatus(text, state) {
    statusText.textContent = text;
    statusIndicator.className = 'status-indicator ' + (state || 'stopped');
}

// Event listeners
startBtn.addEventListener('click', startEmulator);
stopBtn.addEventListener('click', stopEmulator);
resetBtn.addEventListener('click', resetEmulator);
fullscreenBtn.addEventListener('click', toggleFullscreen);

// Handle fullscreen change
document.addEventListener('fullscreenchange', function() {
    if (!document.fullscreenElement) {
        container.classList.remove('fullscreen');
        fullscreenBtn.textContent = 'Fullscreen';
    }
});

// Handle keyboard focus for better input - attach to container since canvas is created dynamically
screenContainer.addEventListener('click', function() {
    const canvas = screenContainer.querySelector('canvas');
    if (canvas) {
        canvas.focus();
    }
});

// Check if ISO file exists
fetch(ISO_PATH, { method: 'HEAD' })
    .then(response => {
        if (response.ok) {
            updateStatus('Ready to start (ISO found)', 'stopped');
        } else {
            updateStatus('Warning: ISO file not found. Run "make web" to build.', 'stopped');
            startBtn.disabled = true;
        }
    })
    .catch(error => {
        updateStatus('Warning: Could not check for ISO file. Run "make web" to build.', 'stopped');
        startBtn.disabled = true;
    });

