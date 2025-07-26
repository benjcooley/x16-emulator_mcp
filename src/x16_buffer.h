/**
 * X16 Emulator Safe Buffer Management
 * 
 * This header provides a safe buffer interface for C/C++ interoperability.
 * Follows the rule that C functions never return allocated memory - instead
 * they use buffer parameters for output.
 */

#ifndef X16_BUFFER_H
#define X16_BUFFER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Buffer structure for safe C/C++ memory management
typedef struct {
    char* data;
    size_t size;      // current used size
    size_t capacity;  // allocated capacity
} x16_buffer_t;

// Initialize buffer structure (does not allocate memory)
void x16_buffer_init(x16_buffer_t* buffer);

// Allocate memory for buffer with specified capacity
bool x16_buffer_alloc(x16_buffer_t* buffer, size_t capacity);

// Free buffer memory and reset structure
void x16_buffer_free(x16_buffer_t* buffer);

// Resize buffer to new capacity (preserves existing data if possible)
bool x16_buffer_resize(x16_buffer_t* buffer, size_t new_capacity);

// Append data to buffer, growing if necessary
bool x16_buffer_append(x16_buffer_t* buffer, const char* data, size_t len);

// Set buffer contents (replaces existing data)
bool x16_buffer_set(x16_buffer_t* buffer, const char* data, size_t len);

// Clear buffer contents (keeps allocated memory)
void x16_buffer_clear(x16_buffer_t* buffer);

// Get null-terminated string from buffer (adds null terminator if needed)
bool x16_buffer_ensure_null_terminated(x16_buffer_t* buffer);

#ifdef __cplusplus
}
#endif

#endif // X16_BUFFER_H
