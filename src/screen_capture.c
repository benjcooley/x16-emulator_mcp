// Commander X16 Emulator - Screen Capture Module Implementation
// Copyright (c) 2025
// Text screen capture functionality for MCP and debugging

#include "screen_capture.h"
#include "video.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PETSCII to ASCII conversion for common characters
static char convert_petscii_to_ascii(uint8_t petscii_code) {
    // Handle special PETSCII codes that are commonly used
    switch (petscii_code) {
        case 0x01: return 'A';  // PETSCII A
        case 0x02: return 'B';  // PETSCII B  
        case 0x03: return 'C';  // PETSCII C
        case 0x04: return 'D';  // PETSCII D
        case 0x05: return 'E';  // PETSCII E
        case 0x06: return 'F';  // PETSCII F
        case 0x07: return 'G';  // PETSCII G
        case 0x08: return 'H';  // PETSCII H
        case 0x09: return 'I';  // PETSCII I
        case 0x0A: return 'J';  // PETSCII J
        case 0x0B: return 'K';  // PETSCII K
        case 0x0C: return 'L';  // PETSCII L
        case 0x0D: return 'M';  // PETSCII M
        case 0x0E: return 'N';  // PETSCII N
        case 0x0F: return 'O';  // PETSCII O
        case 0x10: return 'P';  // PETSCII P
        case 0x11: return 'Q';  // PETSCII Q
        case 0x12: return 'R';  // PETSCII R
        case 0x13: return 'S';  // PETSCII S
        case 0x14: return 'T';  // PETSCII T
        case 0x15: return 'U';  // PETSCII U
        case 0x16: return 'V';  // PETSCII V
        case 0x17: return 'W';  // PETSCII W
        case 0x18: return 'X';  // PETSCII X
        case 0x19: return 'Y';  // PETSCII Y
        case 0x1A: return 'Z';  // PETSCII Z
        case 0x20: return ' ';  // Space
        case 0x2E: return '.';  // Period
        case 0x2F: return '/';  // Slash
        case 0x30: return '0';  // Numbers 0-9
        case 0x31: return '1';
        case 0x32: return '2';
        case 0x33: return '3';
        case 0x34: return '4';
        case 0x35: return '5';
        case 0x36: return '6';
        case 0x37: return '7';
        case 0x38: return '8';
        case 0x39: return '9';
        case 0x3F: return '?';  // Question mark
        default:
            // For standard ASCII range, use as-is
            if (petscii_code >= 32 && petscii_code <= 127) {
                return (char)petscii_code;
            }
            // Fallback for graphics/control characters
            return '.';
    }
}

// Default options
screen_capture_options_t screen_capture_default_options(void) {
    screen_capture_options_t options = {
        .layer = -1,              // Auto-detect active layer
        .include_colors = false,
        .include_cursor = false,
        .format_borders = false,  // Pure text output by default
        .convert_petscii = true   // Enable PETSCII conversion by default
    };
    return options;
}

// Free result structure
void screen_capture_free_result(screen_capture_result_t* result) {
    if (result) {
        if (result->lines) {
            // Free each individual line string
            for (int i = 0; i < result->line_count; i++) {
                if (result->lines[i]) {
                    free(result->lines[i]);
                }
            }
            // Free the array of pointers
            free(result->lines);
            result->lines = NULL;
        }
        if (result->error_message) {
            free(result->error_message);
            result->error_message = NULL;
        }
        result->line_count = 0;
    }
}

// Advanced text capture function with configurable options
screen_capture_result_t screen_capture_text_advanced(const screen_capture_options_t* options) {
    screen_capture_result_t result = {0};
    
    if (!options) {
        result.success = false;
        result.error_message = strdup("Invalid options parameter");
        return result;
    }
    
    // Allocate buffer for raw screen data
    uint32_t width, height;
    size_t buffer_size = 80 * 60 * 2; // Max screen size * 2 bytes per character
    uint8_t* raw_buffer = malloc(buffer_size);
    if (!raw_buffer) {
        result.success = false;
        result.error_message = strdup("Memory allocation failed");
        return result;
    }
    
    // Capture text buffer from video system
    int32_t actual_layer = options->layer;
    bool capture_success = capture_text_buffer(raw_buffer, buffer_size, options->layer, &width, &height, &actual_layer);
    
    if (!capture_success) {
        free(raw_buffer);
        result.success = false;
        result.error_message = strdup("No text layer active or capture failed");
        return result;
    }
    
    // Set result dimensions and layer
    result.width = width;
    result.height = height;
    result.active_layer = actual_layer;
    
    // First pass: collect all lines in a temporary array
    char temp_lines[60][256]; // Max 60 lines, 256 chars each
    uint32_t line_count = 0;
    
    // Process each row of the screen
    for (uint32_t row = 0; row < height; row++) {
        // Build the line content first in a temporary buffer
        char line_content[256]; // Temporary buffer for the line
        size_t line_pos = 0;
        
        // Process each column in the row
        for (uint32_t col = 0; col < width && line_pos < sizeof(line_content) - 1; col++) {
            uint32_t buffer_index = (row * width + col) * 2;
            uint8_t char_code = 0;
            
            // Bounds check and extract character
            if (buffer_index < buffer_size) {
                char_code = raw_buffer[buffer_index];
            }
            
            // Convert character based on options (always use PETSCII conversion)
            char converted_char = convert_petscii_to_ascii(char_code);
            line_content[line_pos++] = converted_char;
        }
        line_content[line_pos] = '\0';
        
        // Trim trailing spaces from the line
        while (line_pos > 0 && line_content[line_pos - 1] == ' ') {
            line_pos--;
        }
        line_content[line_pos] = '\0';
        
        // Store the line
        strncpy(temp_lines[line_count], line_content, sizeof(temp_lines[line_count]) - 1);
        temp_lines[line_count][sizeof(temp_lines[line_count]) - 1] = '\0';
        line_count++;
    }
    
    // Trim empty lines from the end
    while (line_count > 0 && strlen(temp_lines[line_count - 1]) == 0) {
        line_count--;
    }
    
    // Clean up raw buffer
    free(raw_buffer);
    
    // Allocate array of string pointers
    result.lines = malloc(line_count * sizeof(char*));
    if (!result.lines) {
        result.success = false;
        result.error_message = strdup("Failed to allocate lines array");
        return result;
    }
    
    // Allocate and copy each line string
    result.line_count = line_count;
    for (uint32_t i = 0; i < line_count; i++) {
        result.lines[i] = strdup(temp_lines[i]);
        if (!result.lines[i]) {
            // Clean up on failure
            for (uint32_t j = 0; j < i; j++) {
                free(result.lines[j]);
            }
            free(result.lines);
            result.lines = NULL;
            result.line_count = 0;
            result.success = false;
            result.error_message = strdup("Failed to allocate line string");
            return result;
        }
    }
    
    result.success = true;
    return result;
}

// Simple text capture function with default options
char* screen_capture_text(void) {
    screen_capture_options_t options = screen_capture_default_options();
    screen_capture_result_t result = screen_capture_text_advanced(&options);
    
    if (result.success && result.lines && result.line_count > 0) {
        // Build a simple concatenated string from all lines
        size_t total_length = 0;
        for (int i = 0; i < result.line_count; i++) {
            total_length += strlen(result.lines[i]) + 1; // +1 for newline
        }
        total_length += 1; // +1 for null terminator
        
        char* text = malloc(total_length);
        if (text) {
            text[0] = '\0';
            for (int i = 0; i < result.line_count; i++) {
                strcat(text, result.lines[i]);
                if (i < result.line_count - 1) {
                    strcat(text, "\n");
                }
            }
        }
        
        screen_capture_free_result(&result);
        return text ? text : strdup("Error: Memory allocation failed");
    } else {
        screen_capture_free_result(&result);
        return strdup("Error: No text mode active or capture failed");
    }
}
