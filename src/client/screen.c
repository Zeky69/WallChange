#include "client/screen.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void capture_screen(const char *output_file) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Cannot open display\n");
        return; 
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    XWindowAttributes gwa;
    XGetWindowAttributes(display, root, &gwa);
    int width = gwa.width;
    int height = gwa.height;

    XImage *image = XGetImage(display, root, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
        fprintf(stderr, "Cannot capture screen\n");
        XCloseDisplay(display);
        return;
    }

    // Convert XImage to RGB buffer
    unsigned char *rgb_data = malloc(width * height * 3);
    if (!rgb_data) {
        fprintf(stderr, "Memory allocation failed\n");
        XDestroyImage(image);
        XCloseDisplay(display);
        return;
    }

    unsigned long red_mask = image->red_mask;
    unsigned long green_mask = image->green_mask;
    unsigned long blue_mask = image->blue_mask;

    int r_shift = 0; while ((red_mask & 1) == 0 && red_mask != 0) { red_mask >>= 1; r_shift++; }
    int g_shift = 0; while ((green_mask & 1) == 0 && green_mask != 0) { green_mask >>= 1; g_shift++; }
    int b_shift = 0; while ((blue_mask & 1) == 0 && blue_mask != 0) { blue_mask >>= 1; b_shift++; }

    // Restore masks for the loop
    red_mask = image->red_mask;
    green_mask = image->green_mask;
    blue_mask = image->blue_mask;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            
            rgb_data[(y * width + x) * 3 + 0] = (pixel & red_mask) >> r_shift;
            rgb_data[(y * width + x) * 3 + 1] = (pixel & green_mask) >> g_shift;
            rgb_data[(y * width + x) * 3 + 2] = (pixel & blue_mask) >> b_shift;
        }
    }
    
    // Save as JPG
    stbi_write_jpg(output_file, width, height, 3, rgb_data, 70); // Quality 70 to save bandwidth
    
    free(rgb_data);
    XDestroyImage(image);
    XCloseDisplay(display);
}
