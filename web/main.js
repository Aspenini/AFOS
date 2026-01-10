// AFOS Web Emulator - Minimal
let emulator = null;
let isRunning = false;
const ISO_PATH = './afos.iso';

const startBtn = document.getElementById('start-btn');
const screenContainer = document.getElementById('screen-container');
const container = document.querySelector('.container');

// Wait for v86 to load - check both V86Starter and V86
function waitForV86(callback, maxAttempts = 100) {
    let attempts = 0;
    const check = setInterval(() => {
        attempts++;
        // Check for V86Starter or V86 (different versions use different names)
        const V86 = window.V86Starter || window.V86;
        if (typeof V86 !== 'undefined') {
            clearInterval(check);
            window.V86Starter = V86; // Normalize to V86Starter
            console.log('v86 library loaded successfully');
            callback();
        } else if (attempts >= maxAttempts) {
            clearInterval(check);
            console.error('v86 library failed to load after', maxAttempts, 'attempts');
            startBtn.disabled = false;
            startBtn.textContent = 'ERROR: REFRESH PAGE';
            startBtn.style.background = '#ff0000';
            startBtn.style.color = '#fff';
        }
    }, 100);
}

function startEmulator() {
    if (isRunning) {
        return;
    }
    
    if (typeof window.V86Starter === 'undefined') {
        console.error('V86Starter not available');
        startBtn.textContent = 'LOADING...';
        waitForV86(startEmulator);
        return;
    }
    
    isRunning = true;
    startBtn.disabled = true;
    startBtn.textContent = 'STARTING...';
    container.classList.add('running');
    
    try {
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
        
        emulator.add_listener("emulator-started", function() {
            startBtn.style.display = 'none';
        });
        
        emulator.add_listener("error", function(error) {
            console.error('Emulator error:', error);
            startBtn.disabled = false;
            startBtn.textContent = 'ERROR: CLICK TO RETRY';
            startBtn.style.background = '#ff0000';
            startBtn.style.color = '#fff';
            isRunning = false;
            container.classList.remove('running');
        });
        
    } catch (error) {
        console.error('Failed to start emulator:', error);
        startBtn.disabled = false;
        startBtn.textContent = 'ERROR: CLICK TO RETRY';
        startBtn.style.background = '#ff0000';
        startBtn.style.color = '#fff';
        isRunning = false;
        container.classList.remove('running');
    }
}

// Initialize when v86 is loaded
waitForV86(() => {
    startBtn.textContent = 'START';
    startBtn.disabled = false;
});

startBtn.addEventListener('click', startEmulator);

// Focus screen container for keyboard input
screenContainer.addEventListener('click', function() {
    const canvas = screenContainer.querySelector('canvas');
    if (canvas) {
        canvas.focus();
    }
});

