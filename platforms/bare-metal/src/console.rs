use alloc::string::String;
use limine::framebuffer::Framebuffer;
use noto_sans_mono_bitmap::{FontWeight, RasterHeight, get_raster};

use crate::arch::ArchIo;

const FONT_HEIGHT: usize = 16;
const LINE_HEIGHT: usize = FONT_HEIGHT + 2;

pub struct Console {
    arch: ArchIo,
    framebuffer: Option<FramebufferConsole>,
}

impl Console {
    pub fn new(
        framebuffer: Option<&'static Framebuffer>,
        hhdm: u64,
        executable_address: Option<(u64, u64)>,
    ) -> Self {
        Self {
            arch: ArchIo::new(hhdm, executable_address),
            framebuffer: framebuffer.and_then(FramebufferConsole::new),
        }
    }

    pub fn write(&mut self, text: &str) {
        for byte in text.bytes() {
            ArchIo::write_byte(byte);
        }
        if let Some(framebuffer) = &mut self.framebuffer {
            framebuffer.write(text);
        }
    }

    pub fn read_line(&mut self, prompt: &str, secret: bool) -> String {
        self.write(prompt);
        let mut line = String::new();
        loop {
            let Some(byte) = self.arch.poll_byte() else {
                core::hint::spin_loop();
                continue;
            };
            match byte {
                b'\r' | b'\n' => {
                    self.write("\n");
                    return line;
                }
                8 | 127 => {
                    if line.pop().is_some() {
                        self.write("\u{8} \u{8}");
                    }
                }
                b'\t' => {
                    line.push(' ');
                    self.write(if secret { "*" } else { " " });
                }
                value if value.is_ascii_graphic() || value == b' ' => {
                    line.push(char::from(value));
                    if secret {
                        self.write("*");
                    } else {
                        let character = [value];
                        if let Ok(text) = core::str::from_utf8(&character) {
                            self.write(text);
                        }
                    }
                }
                _ => {}
            }
        }
    }

    pub fn clear(&mut self) {
        self.write("\u{1b}[2J\u{1b}[H");
        if let Some(framebuffer) = &mut self.framebuffer {
            framebuffer.clear();
        }
    }

    pub fn poll_cancel(&mut self) -> bool {
        matches!(self.arch.poll_byte(), Some(0x1b))
    }
}

struct FramebufferConsole {
    address: *mut u8,
    width: usize,
    height: usize,
    pitch: usize,
    red_shift: u8,
    green_shift: u8,
    blue_shift: u8,
    cursor_x: usize,
    cursor_y: usize,
}

impl FramebufferConsole {
    fn new(framebuffer: &'static Framebuffer) -> Option<Self> {
        if framebuffer.bpp != 32
            || framebuffer.width < 80
            || framebuffer.height < LINE_HEIGHT as u64
        {
            return None;
        }
        let mut console = Self {
            address: framebuffer.address().cast(),
            width: usize::try_from(framebuffer.width).ok()?,
            height: usize::try_from(framebuffer.height).ok()?,
            pitch: usize::try_from(framebuffer.pitch).ok()?,
            red_shift: framebuffer.red_mask_shift,
            green_shift: framebuffer.green_mask_shift,
            blue_shift: framebuffer.blue_mask_shift,
            cursor_x: 0,
            cursor_y: 0,
        };
        console.clear();
        Some(console)
    }

    fn write(&mut self, text: &str) {
        for character in text.chars() {
            self.put_char(character);
        }
    }

    fn put_char(&mut self, character: char) {
        match character {
            '\n' => {
                self.cursor_x = 0;
                self.cursor_y += LINE_HEIGHT;
                self.ensure_visible();
                return;
            }
            '\r' => {
                self.cursor_x = 0;
                return;
            }
            '\u{8}' => {
                self.cursor_x = self.cursor_x.saturating_sub(9);
                return;
            }
            character if character.is_control() => return,
            _ => {}
        }

        let glyph = get_raster(character, FontWeight::Regular, RasterHeight::Size16)
            .or_else(|| get_raster('?', FontWeight::Regular, RasterHeight::Size16));
        let Some(glyph) = glyph else {
            return;
        };
        if self.cursor_x + glyph.width() + 1 >= self.width {
            self.cursor_x = 0;
            self.cursor_y += LINE_HEIGHT;
            self.ensure_visible();
        }
        for (y, row) in glyph.raster().iter().enumerate() {
            for (x, intensity) in row.iter().copied().enumerate() {
                self.write_pixel(
                    self.cursor_x + x,
                    self.cursor_y + y,
                    self.pixel_color(intensity),
                );
            }
        }
        self.cursor_x += glyph.width() + 1;
    }

    fn ensure_visible(&mut self) {
        if self.cursor_y + LINE_HEIGHT <= self.height {
            return;
        }
        let bytes_to_move = (self.height - LINE_HEIGHT) * self.pitch;
        // SAFETY: Source and destination are within the framebuffer and overlap.
        unsafe {
            core::ptr::copy(
                self.address.add(LINE_HEIGHT * self.pitch),
                self.address,
                bytes_to_move,
            );
            core::ptr::write_bytes(self.address.add(bytes_to_move), 0, LINE_HEIGHT * self.pitch);
        }
        self.cursor_y = self.height - LINE_HEIGHT;
    }

    fn clear(&mut self) {
        // SAFETY: The framebuffer spans `height * pitch` writable bytes.
        unsafe {
            core::ptr::write_bytes(self.address, 0, self.height * self.pitch);
        }
        self.cursor_x = 0;
        self.cursor_y = 0;
    }

    fn pixel_color(&self, intensity: u8) -> u32 {
        let channel = u32::from(intensity);
        (channel << self.red_shift) | (channel << self.green_shift) | (channel << self.blue_shift)
    }

    fn write_pixel(&mut self, x: usize, y: usize, color: u32) {
        if x >= self.width || y >= self.height {
            return;
        }
        // SAFETY: Bounds were checked and the framebuffer uses 32-bit pixels.
        unsafe {
            core::ptr::write_volatile(self.address.add(y * self.pitch + x * 4).cast(), color);
        }
    }
}
