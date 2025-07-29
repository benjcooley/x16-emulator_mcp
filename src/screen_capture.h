// Commander X16 Emulator - Screen Capture Module
// Copyright (c) 2025
// Text screen capture functionality for MCP and debugging

#ifndef SCREEN_CAPTURE_H
#define SCREEN_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screen capture result structure
typedef struct {
    char** lines;           // Array of string pointers for each line (caller must free)
    int line_count;         // Number of lines in the array
    int width;              // Screen width in characters
    int height;             // Screen height in characters
    int active_layer;       // Which layer was captured (0 or 1, -1 if none)
    bool success;           // Whether capture was successful
    char* error_message;    // Error description if success is false (caller must free)
} screen_capture_result_t;

// Screen capture options
typedef struct {
    int layer;              // Layer to capture (0, 1, or -1 for auto-detect)
    bool include_colors;    // Include color/attribute information
    bool include_cursor;    // Include cursor position info
    bool format_borders;    // Add border characters around output
    bool convert_petscii;   // Convert PETSCII graphics to ASCII equivalents
} screen_capture_options_t;

// Main text capture functions
char* screen_capture_text(void);
screen_capture_result_t screen_capture_text_advanced(const screen_capture_options_t* options);

// Utility functions
void screen_capture_get_dimensions(int* width, int* height);
bool screen_capture_is_text_mode_active(void);
int screen_capture_get_active_text_layer(void);

// Helper functions for memory management
void screen_capture_free_result(screen_capture_result_t* result);

// Default options
screen_capture_options_t screen_capture_default_options(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_CAPTURE_H
