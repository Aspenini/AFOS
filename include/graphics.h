#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "types.h"

// Graphics mode constants
#define GFX_MODE_320x200x8   0
#define GFX_MODE_640x480x32  1

// Color structure for 32-bit mode (RGB)
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;  // Reserved for future use
} gfx_color_t;

// Graphics context
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;  // Bits per pixel
    uint8_t* framebuffer;
    uint32_t pitch;  // Bytes per row
    int mode;
} gfx_context_t;

// Public API
int gfx_init(uint32_t width, uint32_t height, uint32_t bpp);
void gfx_shutdown(void);
void gfx_clear(uint32_t color);
void gfx_set_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t gfx_get_pixel(uint32_t x, uint32_t y);
void gfx_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color);
void gfx_fill_circle(uint32_t x, uint32_t y, uint32_t radius, uint32_t color);
void gfx_draw_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color);
void gfx_fill_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, uint32_t color);
void gfx_draw_polygon(uint32_t* x_points, uint32_t* y_points, uint32_t num_points, uint32_t color);
void gfx_fill_polygon(uint32_t* x_points, uint32_t* y_points, uint32_t num_points, uint32_t color);
void gfx_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color);
void gfx_swap_buffers(void);  // For double buffering

// Color helpers
uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t gfx_get_width(void);
uint32_t gfx_get_height(void);

// Graphics demo (built into kernel)
void gfx_demo(void);

#endif

