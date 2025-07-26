/**
 * X16 Emulator Safe Buffer Management Implementation
 */

#include "x16_buffer.h"
#include <stdlib.h>
#include <string.h>

// Initialize buffer structure (does not allocate memory)
void x16_buffer_init(x16_buffer_t* buffer) {
    if (!buffer) return;
    
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

// Allocate memory for buffer with specified capacity
bool x16_buffer_alloc(x16_buffer_t* buffer, size_t capacity) {
    if (!buffer || capacity == 0) {
        return false;
    }
    
    // Free existing memory if any
    if (buffer->data) {
        free(buffer->data);
    }
    
    // Allocate new memory
    buffer->data = (char*)malloc(capacity);
    if (!buffer->data) {
        buffer->size = 0;
        buffer->capacity = 0;
        return false;
    }
    
    buffer->capacity = capacity;
    buffer->size = 0;
    
    // Initialize with null terminator
    if (capacity > 0) {
        buffer->data[0] = '\0';
    }
    
    return true;
}

// Free buffer memory and reset structure
void x16_buffer_free(x16_buffer_t* buffer) {
    if (!buffer) return;
    
    if (buffer->data) {
        free(buffer->data);
    }
    
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

// Resize buffer to new capacity (preserves existing data if possible)
bool x16_buffer_resize(x16_buffer_t* buffer, size_t new_capacity) {
    if (!buffer || new_capacity == 0) {
        return false;
    }
    
    // If no existing data, just allocate
    if (!buffer->data) {
        return x16_buffer_alloc(buffer, new_capacity);
    }
    
    // If new capacity is same as current, nothing to do
    if (new_capacity == buffer->capacity) {
        return true;
    }
    
    // Reallocate memory
    char* new_data = (char*)realloc(buffer->data, new_capacity);
    if (!new_data) {
        return false; // Keep existing buffer on failure
    }
    
    buffer->data = new_data;
    buffer->capacity = new_capacity;
    
    // Adjust size if it exceeds new capacity
    if (buffer->size > new_capacity) {
        buffer->size = new_capacity;
    }
    
    // Ensure null termination if there's space
    if (buffer->size < new_capacity) {
        buffer->data[buffer->size] = '\0';
    }
    
    return true;
}

// Append data to buffer, growing if necessary
bool x16_buffer_append(x16_buffer_t* buffer, const char* data, size_t len) {
    if (!buffer || !data || len == 0) {
        return false;
    }
    
    // Calculate required capacity (including space for null terminator)
    size_t required_capacity = buffer->size + len + 1;
    
    // Grow buffer if needed
    if (required_capacity > buffer->capacity) {
        // Grow by at least 50% or required size, whichever is larger
        size_t new_capacity = buffer->capacity + (buffer->capacity / 2);
        if (new_capacity < required_capacity) {
            new_capacity = required_capacity;
        }
        
        if (!x16_buffer_resize(buffer, new_capacity)) {
            return false;
        }
    }
    
    // Append the data
    memcpy(buffer->data + buffer->size, data, len);
    buffer->size += len;
    
    // Ensure null termination
    if (buffer->size < buffer->capacity) {
        buffer->data[buffer->size] = '\0';
    }
    
    return true;
}

// Set buffer contents (replaces existing data)
bool x16_buffer_set(x16_buffer_t* buffer, const char* data, size_t len) {
    if (!buffer) {
        return false;
    }
    
    // Clear existing data
    buffer->size = 0;
    
    // If no data to set, just clear
    if (!data || len == 0) {
        if (buffer->data && buffer->capacity > 0) {
            buffer->data[0] = '\0';
        }
        return true;
    }
    
    // Append the new data
    return x16_buffer_append(buffer, data, len);
}

// Clear buffer contents (keeps allocated memory)
void x16_buffer_clear(x16_buffer_t* buffer) {
    if (!buffer) return;
    
    buffer->size = 0;
    
    // Add null terminator if buffer is allocated
    if (buffer->data && buffer->capacity > 0) {
        buffer->data[0] = '\0';
    }
}

// Get null-terminated string from buffer (adds null terminator if needed)
bool x16_buffer_ensure_null_terminated(x16_buffer_t* buffer) {
    if (!buffer || !buffer->data) {
        return false;
    }
    
    // If there's space for null terminator, add it
    if (buffer->size < buffer->capacity) {
        buffer->data[buffer->size] = '\0';
        return true;
    }
    
    // Need to grow buffer by 1 to add null terminator
    if (!x16_buffer_resize(buffer, buffer->capacity + 1)) {
        return false;
    }
    
    buffer->data[buffer->size] = '\0';
    return true;
}
