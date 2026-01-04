// Graphics Demo Program for AFOS
// This demonstrates the graphics API

// Forward declarations of kernel functions
// In a real implementation, these would be linked from the kernel
extern int gfx_init(uint32_t width, uint32_t height, uint32_t bpp);
extern void gfx_clear(uint32_t color);
extern void gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color);
extern void gfx_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);
extern void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void gfx_draw_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color);
extern void gfx_fill_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color);
extern uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);
extern uint32_t gfx_get_width(void);
extern uint32_t gfx_get_height(void);
extern void terminal_writestring(const char* data);
extern void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_CYAN 0x0B

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    terminal_writestring_color("AFOS Graphics Demo\n", COLOR_CYAN);
    terminal_writestring("Initializing graphics (640x480x32)...\n");
    
    // Initialize graphics mode
    if (gfx_init(640, 480, 32) != 0) {
        terminal_writestring("Failed to initialize graphics!\n");
        return 1;
    }
    
    terminal_writestring("Graphics initialized successfully!\n");
    terminal_writestring("Drawing test patterns...\n");
    
    // Clear screen with dark blue
    uint32_t dark_blue = gfx_rgb(0, 0, 64);
    gfx_clear(dark_blue);
    
    // Draw some shapes
    uint32_t white = gfx_rgb(255, 255, 255);
    uint32_t red = gfx_rgb(255, 0, 0);
    uint32_t green = gfx_rgb(0, 255, 0);
    uint32_t blue = gfx_rgb(0, 0, 255);
    uint32_t yellow = gfx_rgb(255, 255, 0);
    uint32_t cyan = gfx_rgb(0, 255, 255);
    uint32_t magenta = gfx_rgb(255, 0, 255);
    
    // Draw some rectangles
    gfx_fill_rect(50, 50, 100, 100, red);
    gfx_draw_rect(50, 50, 100, 100, white);
    
    gfx_fill_rect(200, 50, 100, 100, green);
    gfx_draw_rect(200, 50, 100, 100, white);
    
    gfx_fill_rect(350, 50, 100, 100, blue);
    gfx_draw_rect(350, 50, 100, 100, white);
    
    // Draw some circles
    gfx_fill_circle(150, 250, 50, yellow);
    gfx_draw_circle(150, 250, 50, white);
    
    gfx_fill_circle(300, 250, 50, cyan);
    gfx_draw_circle(300, 250, 50, white);
    
    gfx_fill_circle(450, 250, 50, magenta);
    gfx_draw_circle(450, 250, 50, white);
    
    // Draw some lines
    gfx_draw_line(50, 350, 590, 350, white);
    gfx_draw_line(50, 400, 590, 400, white);
    gfx_draw_line(50, 450, 590, 450, white);
    
    // Draw diagonal lines
    gfx_draw_line(100, 350, 200, 450, red);
    gfx_draw_line(250, 350, 350, 450, green);
    gfx_draw_line(400, 350, 500, 450, blue);
    
    terminal_writestring("Graphics demo complete!\n");
    terminal_writestring("Note: Framebuffer is in memory. To display it, ");
    terminal_writestring("you would need to copy it to a graphics mode framebuffer.\n");
    
    return 0;
}

