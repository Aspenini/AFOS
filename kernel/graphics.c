#include "graphics.h"
#include "vesa.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);
void* malloc(uint32_t size);

// Color constants
#define COLOR_RED 0x0C

// Graphics context (global)
static gfx_context_t gfx_ctx = {0};
static uint8_t* gfx_backbuffer = NULL;  // For double buffering
static uint8_t* gfx_allocated_fb = NULL;  // Track if we allocated framebuffer (vs VESA)

// Simple framebuffer allocation
// We'll use a fixed-size framebuffer in memory
#define GFX_FB_SIZE (1024 * 768 * 4)  // Max 1024x768x32bpp

// Initialize graphics system
int gfx_init(uint32_t width, uint32_t height, uint32_t bpp) {
    // Check if already initialized
    if (gfx_ctx.framebuffer != NULL) {
        return -1;  // Already initialized
    }
    
    // Validate parameters
    if (width == 0 || height == 0 || (bpp != 8 && bpp != 32)) {
        terminal_writestring_color("Error: Invalid graphics mode\n", COLOR_RED);
        return -1;
    }
    
    // Switch to VGA graphics mode (mode 13h: 320x200x8)
    // This is direct VGA programming - works in BIOS mode without GRUB
    if (vesa_set_mode((uint16_t)width, (uint16_t)height, (uint8_t)bpp) == 0) {
        // VGA mode switched successfully
        uint32_t vga_fb = vesa_get_framebuffer_addr();
        if (vga_fb != 0) {
            // Use VGA framebuffer directly (0xA0000 for mode 13h)
            gfx_ctx.framebuffer = (uint8_t*)vga_fb;
            gfx_allocated_fb = NULL; // Not our allocation
            
            // Use actual VGA mode dimensions
            uint16_t actual_width = vesa_get_width();
            uint16_t actual_height = vesa_get_height();
            uint8_t actual_bpp = vesa_get_bpp();
            if (actual_width > 0 && actual_height > 0) {
                gfx_ctx.width = actual_width;
                gfx_ctx.height = actual_height;
                gfx_ctx.bpp = actual_bpp;
                // For 8-bit mode, pitch equals width (linear framebuffer)
                gfx_ctx.pitch = (actual_bpp == 8) ? actual_width : vesa_get_pitch();
            } else {
                // Fallback to requested dimensions
                gfx_ctx.width = width;
                gfx_ctx.height = height;
                gfx_ctx.bpp = bpp;
                // For 8-bit mode, pitch equals width
                gfx_ctx.pitch = width * (bpp / 8);
            }
        } else {
            // VGA mode set but no framebuffer address - shouldn't happen
            terminal_writestring_color("Error: VGA mode set but no framebuffer address\n", COLOR_RED);
            vesa_switch_to_text_mode();
            return -1;
        }
    } else {
        // Mode switch failed
        terminal_writestring_color("Error: Failed to switch to VGA graphics mode\n", COLOR_RED);
        return -1;
    }
    
    // For VGA mode 13h, disable double buffering (write directly to framebuffer)
    // Double buffering can cause issues with the hardware framebuffer
    gfx_backbuffer = NULL;
    
    gfx_allocated_fb = NULL;
    
    // Set mode
    gfx_ctx.mode = (gfx_ctx.bpp == 8) ? GFX_MODE_320x200x8 : GFX_MODE_640x480x32;
    
    // Verify dimensions are correct (should be 320x200 for mode 13h)
    // Force correct dimensions if they don't match
    if (gfx_ctx.width != 320 || gfx_ctx.height != 200) {
        gfx_ctx.width = 320;
        gfx_ctx.height = 200;
        gfx_ctx.pitch = 320;
    }
    
    // Clear framebuffer with black
    // For VGA mode 13h, we need to clear the entire framebuffer
    uint32_t fb_size = gfx_ctx.width * gfx_ctx.height;
    uint8_t* fb = gfx_ctx.framebuffer;
    for (uint32_t i = 0; i < fb_size; i++) {
        fb[i] = 0;  // Black (palette index 0)
    }
    
    return 0;
}

// Shutdown graphics system
void gfx_shutdown(void) {
    // Switch back to text mode
    vesa_switch_to_text_mode();
    
    // Framebuffer will be freed by malloc_reset (if it was malloc'd)
    // If it was VESA framebuffer, it's not ours to free
    gfx_ctx.framebuffer = NULL;
    gfx_allocated_fb = NULL;
    gfx_backbuffer = NULL;
    gfx_ctx.width = 0;
    gfx_ctx.height = 0;
}

// Clear screen with color (clears backbuffer if available)
void gfx_clear(uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    uint8_t* target = (gfx_backbuffer != NULL) ? gfx_backbuffer : gfx_ctx.framebuffer;
    
    if (gfx_ctx.bpp == 32) {
        // 32-bit mode: fill with color
        uint32_t size = gfx_ctx.width * gfx_ctx.height;
        uint32_t* fb = (uint32_t*)target;
        for (uint32_t i = 0; i < size; i++) {
            fb[i] = color;
        }
    } else {
        // 8-bit mode: fill with color (low byte) - linear framebuffer
        uint32_t size = gfx_ctx.width * gfx_ctx.height;
        uint8_t c = (uint8_t)(color & 0xFF);
        for (uint32_t i = 0; i < size; i++) {
            target[i] = c;
        }
    }
}

// Set pixel color (writes to backbuffer if available, otherwise directly to framebuffer)
void gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    if (x >= gfx_ctx.width || y >= gfx_ctx.height) return;
    
    // Calculate offset - for 8-bit mode, pitch equals width (linear framebuffer)
    uint32_t offset;
    if (gfx_ctx.bpp == 8) {
        offset = y * gfx_ctx.width + x;  // Linear: y * width + x
    } else {
        offset = y * gfx_ctx.pitch + x * (gfx_ctx.bpp / 8);
    }
    
    uint8_t* target = (gfx_backbuffer != NULL) ? gfx_backbuffer : gfx_ctx.framebuffer;
    
    if (gfx_ctx.bpp == 32) {
        // 32-bit mode: store as RGBA
        uint32_t* pixel = (uint32_t*)(target + offset);
        *pixel = color;
    } else {
        // 8-bit mode: store as palette index
        target[offset] = (uint8_t)(color & 0xFF);
    }
}

// Get pixel color
uint32_t gfx_get_pixel(uint32_t x, uint32_t y) {
    if (gfx_ctx.framebuffer == NULL) return 0;
    if (x >= gfx_ctx.width || y >= gfx_ctx.height) return 0;
    
    // Calculate offset - for 8-bit mode, pitch equals width (linear framebuffer)
    uint32_t offset;
    if (gfx_ctx.bpp == 8) {
        offset = y * gfx_ctx.width + x;  // Linear: y * width + x
    } else {
        offset = y * gfx_ctx.pitch + x * (gfx_ctx.bpp / 8);
    }
    
    if (gfx_ctx.bpp == 32) {
        uint32_t* pixel = (uint32_t*)(gfx_ctx.framebuffer + offset);
        return *pixel;
    } else {
        return (uint32_t)gfx_ctx.framebuffer[offset];
    }
}

// Draw line using Bresenham's algorithm
void gfx_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    // Handle edge cases
    if (x1 == x2 && y1 == y2) {
        gfx_set_pixel(x1, y1, color);
        return;
    }
    
    int32_t dx = (x2 > x1) ? (int32_t)(x2 - x1) : (int32_t)(x1 - x2);
    int32_t dy = (y2 > y1) ? (int32_t)(y2 - y1) : (int32_t)(y1 - y2);
    int32_t sx = (x1 < x2) ? 1 : -1;
    int32_t sy = (y1 < y2) ? 1 : -1;
    int32_t err = dx - dy;
    
    int32_t x = (int32_t)x1;
    int32_t y = (int32_t)y1;
    
    while (1) {
        if (x >= 0 && x < (int32_t)gfx_ctx.width && 
            y >= 0 && y < (int32_t)gfx_ctx.height) {
            gfx_set_pixel((uint32_t)x, (uint32_t)y, color);
        }
        
        if (x == (int32_t)x2 && y == (int32_t)y2) break;
        
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

// Draw rectangle outline
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    // Top
    gfx_draw_line(x, y, x + w - 1, y, color);
    // Bottom
    gfx_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    // Left
    gfx_draw_line(x, y, x, y + h - 1, color);
    // Right
    gfx_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

// Fill rectangle (optimized - writes directly to framebuffer)
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    // Clamp to screen bounds
    if (x >= gfx_ctx.width || y >= gfx_ctx.height) return;
    if (x + w > gfx_ctx.width) w = gfx_ctx.width - x;
    if (y + h > gfx_ctx.height) h = gfx_ctx.height - y;
    
    uint8_t* target = gfx_ctx.framebuffer;
    uint8_t c = (uint8_t)(color & 0xFF);
    
    // For 8-bit mode, use direct memory writes (much faster)
    if (gfx_ctx.bpp == 8) {
        for (uint32_t py = 0; py < h; py++) {
            uint32_t offset = (y + py) * gfx_ctx.width + x;
            for (uint32_t px = 0; px < w; px++) {
                target[offset + px] = c;
            }
        }
    } else {
        // 32-bit mode
        for (uint32_t py = y; py < y + h; py++) {
            for (uint32_t px = x; px < x + w; px++) {
                gfx_set_pixel(px, py, color);
            }
        }
    }
}

// Draw circle outline
void gfx_draw_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    int32_t r = (int32_t)radius;
    int32_t cx = (int32_t)x;
    int32_t cy = (int32_t)y;
    
    int32_t px = 0;
    int32_t py = r;
    int32_t d = 1 - r;
    
    // Draw 8 points of symmetry
    #define CIRCLE_PLOT(cx, cy, x, y) \
        gfx_set_pixel((cx) + (x), (cy) + (y), color); \
        gfx_set_pixel((cx) - (x), (cy) + (y), color); \
        gfx_set_pixel((cx) + (x), (cy) - (y), color); \
        gfx_set_pixel((cx) - (x), (cy) - (y), color); \
        gfx_set_pixel((cx) + (y), (cy) + (x), color); \
        gfx_set_pixel((cx) - (y), (cy) + (x), color); \
        gfx_set_pixel((cx) + (y), (cy) - (x), color); \
        gfx_set_pixel((cx) - (y), (cy) - (x), color);
    
    CIRCLE_PLOT(cx, cy, px, py);
    
    while (px < py) {
        if (d < 0) {
            d += 2 * px + 3;
        } else {
            d += 2 * (px - py) + 5;
            py--;
        }
        px++;
        CIRCLE_PLOT(cx, cy, px, py);
    }
}

// Simple integer square root (for filled circle)
static uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// Fill circle (optimized - writes directly to framebuffer)
void gfx_fill_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    int32_t r = (int32_t)radius;
    int32_t cx = (int32_t)x;
    int32_t cy = (int32_t)y;
    uint8_t* target = gfx_ctx.framebuffer;
    uint8_t c = (uint8_t)(color & 0xFF);
    
    // Draw filled circle by drawing horizontal lines (optimized for 8-bit)
    if (gfx_ctx.bpp == 8) {
        for (int32_t py = -r; py <= r; py++) {
            int32_t py_sq = py * py;
            int32_t r_sq = r * r;
            if (py_sq > r_sq) continue;
            
            int32_t px = (int32_t)isqrt((uint32_t)(r_sq - py_sq));
            int32_t fy = cy + py;
            
            if (fy >= 0 && fy < (int32_t)gfx_ctx.height) {
                // Draw horizontal line directly
                for (int32_t px2 = -px; px2 <= px; px2++) {
                    int32_t fx = cx + px2;
                    if (fx >= 0 && fx < (int32_t)gfx_ctx.width) {
                        uint32_t offset = (uint32_t)fy * gfx_ctx.width + (uint32_t)fx;
                        target[offset] = c;
                    }
                }
            }
        }
    } else {
        // 32-bit mode - use gfx_set_pixel
        for (int32_t py = -r; py <= r; py++) {
            int32_t py_sq = py * py;
            int32_t r_sq = r * r;
            if (py_sq > r_sq) continue;
            
            int32_t px = (int32_t)isqrt((uint32_t)(r_sq - py_sq));
            for (int32_t px2 = -px; px2 <= px; px2++) {
                int32_t fx = cx + px2;
                int32_t fy = cy + py;
                if (fx >= 0 && fx < (int32_t)gfx_ctx.width && 
                    fy >= 0 && fy < (int32_t)gfx_ctx.height) {
                    gfx_set_pixel((uint32_t)fx, (uint32_t)fy, color);
                }
            }
        }
    }
}

// Swap buffers (copy backbuffer to frontbuffer)
void gfx_swap_buffers(void) {
    if (gfx_ctx.framebuffer == NULL || gfx_backbuffer == NULL) return;
    
    uint32_t size = gfx_ctx.width * gfx_ctx.height * (gfx_ctx.bpp / 8);
    for (uint32_t i = 0; i < size; i++) {
        gfx_ctx.framebuffer[i] = gfx_backbuffer[i];
    }
}

// Draw triangle outline
void gfx_draw_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color) {
    gfx_draw_line(x1, y1, x2, y2, color);
    gfx_draw_line(x2, y2, x3, y3, color);
    gfx_draw_line(x3, y3, x1, y1, color);
}

// Fill triangle using scanline algorithm
void gfx_fill_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color) {
    // Sort vertices by y coordinate
    uint32_t tx, ty;
    if (y1 > y2) { tx = x1; ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    if (y1 > y3) { tx = x1; ty = y1; x1 = x3; y1 = y3; x3 = tx; y3 = ty; }
    if (y2 > y3) { tx = x2; ty = y2; x2 = x3; y2 = y3; x3 = tx; y3 = ty; }
    
    uint8_t* target = gfx_ctx.framebuffer;
    uint8_t c = (uint8_t)(color & 0xFF);
    
    if (gfx_ctx.bpp == 8) {
        // Optimized 8-bit mode
        if (y2 == y3) {
            // Flat bottom triangle
            int32_t xl = (int32_t)x1, xr = (int32_t)x1;
            int32_t dx1 = (y1 != y2) ? ((int32_t)x2 - (int32_t)x1) / ((int32_t)y2 - (int32_t)y1) : 0;
            int32_t dx2 = (y1 != y3) ? ((int32_t)x3 - (int32_t)x1) / ((int32_t)y3 - (int32_t)y1) : 0;
            
            for (int32_t y = (int32_t)y1; y <= (int32_t)y2; y++) {
                if (xl > xr) { int32_t t = xl; xl = xr; xr = t; }
                if (y >= 0 && y < (int32_t)gfx_ctx.height) {
                    uint32_t base_offset = (uint32_t)y * gfx_ctx.width;
                    for (int32_t x = xl; x <= xr; x++) {
                        if (x >= 0 && x < (int32_t)gfx_ctx.width) {
                            target[base_offset + (uint32_t)x] = c;
                        }
                    }
                }
                xl += dx1;
                xr += dx2;
            }
        } else if (y1 == y2) {
            // Flat top triangle
            int32_t xl = (int32_t)x1, xr = (int32_t)x2;
            int32_t dx1 = ((int32_t)x3 - (int32_t)x1) / ((int32_t)y3 - (int32_t)y1);
            int32_t dx2 = ((int32_t)x3 - (int32_t)x2) / ((int32_t)y3 - (int32_t)y2);
            
            for (int32_t y = (int32_t)y1; y <= (int32_t)y3; y++) {
                if (xl > xr) { int32_t t = xl; xl = xr; xr = t; }
                if (y >= 0 && y < (int32_t)gfx_ctx.height) {
                    uint32_t base_offset = (uint32_t)y * gfx_ctx.width;
                    for (int32_t x = xl; x <= xr; x++) {
                        if (x >= 0 && x < (int32_t)gfx_ctx.width) {
                            target[base_offset + (uint32_t)x] = c;
                        }
                    }
                }
                xl += dx1;
                xr += dx2;
            }
        } else {
            // General case: split into two triangles
            uint32_t x4 = x1 + ((int32_t)(x3 - x1) * (int32_t)(y2 - y1)) / ((int32_t)(y3 - y1));
            gfx_fill_triangle(x1, y1, x2, y2, x4, y2, color);
            gfx_fill_triangle(x2, y2, x3, y3, x4, y2, color);
        }
    } else {
        // 32-bit mode - use gfx_set_pixel
        if (y2 == y3) {
            // Flat bottom triangle
            int32_t xl = (int32_t)x1, xr = (int32_t)x1;
            int32_t dx1 = (y1 != y2) ? ((int32_t)x2 - (int32_t)x1) / ((int32_t)y2 - (int32_t)y1) : 0;
            int32_t dx2 = (y1 != y3) ? ((int32_t)x3 - (int32_t)x1) / ((int32_t)y3 - (int32_t)y1) : 0;
            
            for (int32_t y = (int32_t)y1; y <= (int32_t)y2; y++) {
                if (xl > xr) { int32_t t = xl; xl = xr; xr = t; }
                for (int32_t x = xl; x <= xr; x++) {
                    if (x >= 0 && x < (int32_t)gfx_ctx.width && y >= 0 && y < (int32_t)gfx_ctx.height) {
                        gfx_set_pixel((uint32_t)x, (uint32_t)y, color);
                    }
                }
                xl += dx1;
                xr += dx2;
            }
        } else if (y1 == y2) {
            // Flat top triangle
            int32_t xl = (int32_t)x1, xr = (int32_t)x2;
            int32_t dx1 = ((int32_t)x3 - (int32_t)x1) / ((int32_t)y3 - (int32_t)y1);
            int32_t dx2 = ((int32_t)x3 - (int32_t)x2) / ((int32_t)y3 - (int32_t)y2);
            
            for (int32_t y = (int32_t)y1; y <= (int32_t)y3; y++) {
                if (xl > xr) { int32_t t = xl; xl = xr; xr = t; }
                for (int32_t x = xl; x <= xr; x++) {
                    if (x >= 0 && x < (int32_t)gfx_ctx.width && y >= 0 && y < (int32_t)gfx_ctx.height) {
                        gfx_set_pixel((uint32_t)x, (uint32_t)y, color);
                    }
                }
                xl += dx1;
                xr += dx2;
            }
        } else {
            // General case: split into two triangles
            uint32_t x4 = x1 + ((int32_t)(x3 - x1) * (int32_t)(y2 - y1)) / ((int32_t)(y3 - y1));
            gfx_fill_triangle(x1, y1, x2, y2, x4, y2, color);
            gfx_fill_triangle(x2, y2, x3, y3, x4, y2, color);
        }
    }
}

// Draw polygon outline
void gfx_draw_polygon(uint32_t* x_points, uint32_t* y_points, uint32_t num_points, uint32_t color) {
    if (num_points < 2) return;
    
    for (uint32_t i = 0; i < num_points - 1; i++) {
        gfx_draw_line(x_points[i], y_points[i], x_points[i + 1], y_points[i + 1], color);
    }
    // Close the polygon
    gfx_draw_line(x_points[num_points - 1], y_points[num_points - 1], x_points[0], y_points[0], color);
}

// Fill polygon using scanline algorithm
void gfx_fill_polygon(uint32_t* x_points, uint32_t* y_points, uint32_t num_points, uint32_t color) {
    if (num_points < 3) return;
    
    // Find bounding box
    uint32_t min_y = y_points[0], max_y = y_points[0];
    for (uint32_t i = 1; i < num_points; i++) {
        if (y_points[i] < min_y) min_y = y_points[i];
        if (y_points[i] > max_y) max_y = y_points[i];
    }
    
    // Scanline fill
    for (uint32_t y = min_y; y <= max_y && y < gfx_ctx.height; y++) {
        // Find intersections with polygon edges
        uint32_t intersections[64]; // Max 64 intersections per scanline
        uint32_t num_intersections = 0;
        
        for (uint32_t i = 0; i < num_points; i++) {
            uint32_t j = (i + 1) % num_points;
            uint32_t y1 = y_points[i], y2 = y_points[j];
            
            if ((y1 < y && y2 >= y) || (y2 < y && y1 >= y)) {
                if (y1 != y2) {
                    int32_t x = (int32_t)x_points[i] + ((int32_t)(y - y1) * ((int32_t)x_points[j] - (int32_t)x_points[i])) / ((int32_t)(y2 - y1));
                    if (num_intersections < 64) {
                        intersections[num_intersections++] = (uint32_t)x;
                    }
                }
            }
        }
        
        // Sort intersections
        for (uint32_t i = 0; i < num_intersections - 1; i++) {
            for (uint32_t j = i + 1; j < num_intersections; j++) {
                if (intersections[i] > intersections[j]) {
                    uint32_t t = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = t;
                }
            }
        }
        
        // Fill between pairs of intersections
        for (uint32_t i = 0; i < num_intersections; i += 2) {
            if (i + 1 < num_intersections) {
                for (uint32_t x = intersections[i]; x <= intersections[i + 1] && x < gfx_ctx.width; x++) {
                    gfx_set_pixel(x, y, color);
                }
            }
        }
    }
}

// Simple 8x8 bitmap font (ASCII characters 32-126)
static const uint8_t font_8x8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space (32)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x06, 0x00}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x3E}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x06, 0x00}, // ;
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // <
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // =
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // >
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // ?
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // @
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // A
    {0x1F, 0x36, 0x36, 0x1E, 0x36, 0x36, 0x1F, 0x00}, // B
    {0x1E, 0x33, 0x03, 0x03, 0x03, 0x33, 0x1E, 0x00}, // C
    {0x1F, 0x36, 0x33, 0x33, 0x33, 0x36, 0x1F, 0x00}, // D
    {0x3F, 0x06, 0x06, 0x1E, 0x06, 0x06, 0x3F, 0x00}, // E
    {0x3F, 0x06, 0x06, 0x1E, 0x06, 0x06, 0x06, 0x00}, // F
    {0x1E, 0x33, 0x03, 0x03, 0x73, 0x33, 0x1E, 0x00}, // G
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // H
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // I
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // J
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // K
    {0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x3F, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // M
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // N
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // O
    {0x1F, 0x33, 0x33, 0x1F, 0x03, 0x03, 0x03, 0x00}, // P
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // Q
    {0x1F, 0x33, 0x33, 0x1F, 0x1B, 0x33, 0x33, 0x00}, // R
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // S
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // T
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // X
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // Y
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // Z
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // [
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // backslash
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ]
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F}, // _
    {0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // a
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // b
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // c
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, // d
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // e
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, // f
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // g
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // h
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // i
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // j
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // k
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // l
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // n
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // o
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // p
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // q
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // s
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // t
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // x
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // y
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // z
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // {
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // }
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
};

// Draw text at position
void gfx_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color) {
    uint32_t cx = x;
    uint32_t cy = y;
    
    while (*text != '\0') {
        if (*text == '\n') {
            cx = x;
            cy += 8;
        } else if (*text >= 32 && *text <= 126) {
            const uint8_t* glyph = font_8x8[*text - 32];
            for (uint32_t row = 0; row < 8; row++) {
                uint8_t bits = glyph[row];
                for (uint32_t col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        if (cx + col < gfx_ctx.width && cy + row < gfx_ctx.height) {
                            gfx_set_pixel(cx + col, cy + row, color);
                        }
                    }
                }
            }
            cx += 8;
        }
        text++;
    }
}

// Color helper: create RGB color
uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (gfx_ctx.bpp == 32) {
        // 32-bit: RGBA format (little-endian: ABGR)
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    } else {
        // 8-bit: simple palette index (just use one component)
        return (uint32_t)((r + g + b) / 3);
    }
}

// Get graphics width
uint32_t gfx_get_width(void) {
    return gfx_ctx.width;
}

// Get graphics height
uint32_t gfx_get_height(void) {
    return gfx_ctx.height;
}

// Graphics demo function (built into kernel)
void gfx_demo(void) {
    if (gfx_ctx.framebuffer == NULL) return;
    
    // Define colors (for 8-bit mode, use palette indices)
    // VGA mode 13h has a standard 256-color palette
    // We'll use simple color indices for the demo
    uint32_t dark_blue = 0;  // Black (palette index 0 - most reliable)
    uint32_t white = 15;     // White in VGA palette
    uint32_t red = 4;        // Red in VGA palette
    uint32_t green = 2;      // Green in VGA palette
    uint32_t blue = 9;       // Light blue in VGA palette
    uint32_t yellow = 14;    // Yellow in VGA palette
    uint32_t cyan = 11;      // Cyan in VGA palette
    uint32_t magenta = 13;   // Magenta in VGA palette
    
    // Clear screen with dark blue background
    gfx_clear(dark_blue);
    
    // Draw some filled rectangles (scaled for 320x200)
    gfx_fill_rect(20, 20, 60, 60, red);
    gfx_draw_rect(20, 20, 60, 60, white);
    
    gfx_fill_rect(120, 20, 60, 60, green);
    gfx_draw_rect(120, 20, 60, 60, white);
    
    gfx_fill_rect(220, 20, 60, 60, blue);
    gfx_draw_rect(220, 20, 60, 60, white);
    
    // Draw some filled circles (scaled for 320x200)
    gfx_fill_circle(80, 120, 30, yellow);
    gfx_draw_circle(80, 120, 30, white);
    
    gfx_fill_circle(160, 120, 30, cyan);
    gfx_draw_circle(160, 120, 30, white);
    
    gfx_fill_circle(240, 120, 30, magenta);
    gfx_draw_circle(240, 120, 30, white);
    
    // Draw horizontal lines
    gfx_draw_line(10, 160, 310, 160, white);
    gfx_draw_line(10, 180, 310, 180, white);
    
    // Draw diagonal lines
    gfx_draw_line(30, 160, 80, 190, red);
    gfx_draw_line(130, 160, 180, 190, green);
    gfx_draw_line(230, 160, 280, 190, blue);
    
    // Draw a border around the screen
    gfx_draw_rect(0, 0, gfx_ctx.width, gfx_ctx.height, white);
    
    // Draw triangles
    gfx_fill_triangle(50, 10, 30, 30, 70, 30, yellow);
    gfx_draw_triangle(50, 10, 30, 30, 70, 30, white);
    
    gfx_fill_triangle(150, 10, 130, 30, 170, 30, cyan);
    gfx_draw_triangle(150, 10, 130, 30, 170, 30, white);
    
    // Draw polygon (hexagon)
    uint32_t hex_x[6] = {260, 280, 280, 260, 240, 240};
    uint32_t hex_y[6] = {20, 30, 50, 60, 50, 30};
    gfx_fill_polygon(hex_x, hex_y, 6, magenta);
    gfx_draw_polygon(hex_x, hex_y, 6, white);
    
    // Draw text
    gfx_draw_text(10, 5, "AFOS Graphics", white);
    gfx_draw_text(10, 190, "Triangles & Text!", yellow);
    
    // No need to swap buffers - we're writing directly to framebuffer
}

