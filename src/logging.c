/**
 * X16 Emulator Logging System Implementation
 */

#define X16_LOGGING_IMPLEMENTATION
#include "logging.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL.h>

// Global logging state
int x16_logging_initialized = 0;
static FILE *x16_log_file = NULL;
static char *log_filename = NULL;

// Checkpoint-based error tracking state
static bool checkpoint_active = false;
static x16_log_entry_t *checkpoint_entries = NULL;
static int checkpoint_count = 0;
static int checkpoint_capacity = 0;

// Helper function to check if a path is absolute
static bool is_absolute_path(const char *path) {
    if (!path || !*path) {
        return false;
    }
    
#ifdef _WIN32
    // Windows: Check for drive letter (C:) or UNC path (\\)
    return (strlen(path) >= 2 && path[1] == ':') || 
           (strlen(path) >= 2 && path[0] == '\\' && path[1] == '\\');
#else
    // Unix-like systems: Check for leading slash
    return path[0] == '/';
#endif
}

// Helper function to resolve relative paths against SDL_GetBasePath()
static char* resolve_log_path(const char *path) {
    if (!path) {
        return NULL;
    }
    
    // If path is absolute, return a copy
    if (is_absolute_path(path)) {
        return strdup(path);
    }
    
    // Path is relative, resolve against executable directory
    char *base_path = SDL_GetBasePath();
    if (!base_path) {
        // Fallback to current directory if SDL_GetBasePath() fails
        return strdup(path);
    }
    
    // Calculate required buffer size
    size_t base_len = strlen(base_path);
    size_t path_len = strlen(path);
    size_t total_len = base_len + path_len + 1; // +1 for null terminator
    
    // Allocate buffer and construct full path
    char *full_path = malloc(total_len);
    if (!full_path) {
        SDL_free(base_path);
        return NULL;
    }
    
    strcpy(full_path, base_path);
    strcat(full_path, path);
    
    SDL_free(base_path);
    return full_path;
}

void x16_logging_init(FILE *log_file) {
    if (x16_logging_initialized) {
        return; // Already initialized
    }
    
    // Set log level to DEBUG to see all messages
    log_set_level(LOG_DEBUG);
    
    // Don't suppress console output
    log_set_quiet(false);
    
    // If a log file is provided, add it as an output
    if (log_file != NULL) {
        x16_log_file = log_file;
        log_add_fp(log_file, LOG_TRACE);
    }
    
    x16_logging_initialized = 1;
    
    // Log initialization message
    log_info("X16 Emulator logging system initialized");
}

void x16_logging_set_file(const char *filename) {
    if (!filename) {
        return;
    }
    
    // Free existing filename if any
    if (log_filename) {
        free(log_filename);
        log_filename = NULL;
    }
    
    // Resolve the path (relative paths are resolved against executable directory)
    log_filename = resolve_log_path(filename);
    if (!log_filename) {
        log_error("Failed to resolve log file path: %s", filename);
        return;
    }
    
    // Close existing log file if any
    if (x16_log_file && x16_log_file != stdout && x16_log_file != stderr) {
        fclose(x16_log_file);
        x16_log_file = NULL;
    }
    
    // Open new log file
    x16_log_file = fopen(log_filename, "w");
    if (!x16_log_file) {
        log_error("Failed to open log file: %s", log_filename);
        return;
    }
    
    // Add the file as a log output if logging is initialized
    if (x16_logging_initialized) {
        log_add_fp(x16_log_file, LOG_TRACE);
    }
    
    log_info("Log file set to: %s", log_filename);
}

void x16_logging_cleanup(void) {
    if (!x16_logging_initialized) {
        return;
    }
    
    log_info("X16 Emulator logging system shutting down");
    
    // Close log file if we opened it
    if (x16_log_file && x16_log_file != stdout && x16_log_file != stderr) {
        fclose(x16_log_file);
        x16_log_file = NULL;
    }
    
    // Free filename
    if (log_filename) {
        free(log_filename);
        log_filename = NULL;
    }
    
    x16_logging_initialized = 0;
}

// Get string representation of log level
const char* x16_log_level_string(x16_log_level_t level) {
    switch (level) {
        case X16_LOG_LEVEL_TRACE: return "TRACE";
        case X16_LOG_LEVEL_DEBUG: return "DEBUG";
        case X16_LOG_LEVEL_INFO:  return "INFO";
        case X16_LOG_LEVEL_WARN:  return "WARN";
        case X16_LOG_LEVEL_ERROR: return "ERROR";
        case X16_LOG_LEVEL_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// Parse log level from string
x16_log_level_t x16_parse_log_level(const char *level_str) {
    if (!level_str) {
        return X16_LOG_LEVEL_INFO; // default
    }
    
    if (strcmp(level_str, "TRACE") == 0) return X16_LOG_LEVEL_TRACE;
    if (strcmp(level_str, "DEBUG") == 0) return X16_LOG_LEVEL_DEBUG;
    if (strcmp(level_str, "INFO") == 0) return X16_LOG_LEVEL_INFO;
    if (strcmp(level_str, "WARN") == 0) return X16_LOG_LEVEL_WARN;
    if (strcmp(level_str, "ERROR") == 0) return X16_LOG_LEVEL_ERROR;
    if (strcmp(level_str, "FATAL") == 0) return X16_LOG_LEVEL_FATAL;
    
    return X16_LOG_LEVEL_INFO; // default for unknown strings
}

// Set log level
void x16_logging_set_level(x16_log_level_t level) {
    // Map X16 log levels to log.c levels
    int log_c_level;
    switch (level) {
        case X16_LOG_LEVEL_TRACE: log_c_level = LOG_TRACE; break;
        case X16_LOG_LEVEL_DEBUG: log_c_level = LOG_DEBUG; break;
        case X16_LOG_LEVEL_INFO:  log_c_level = LOG_INFO; break;
        case X16_LOG_LEVEL_WARN:  log_c_level = LOG_WARN; break;
        case X16_LOG_LEVEL_ERROR: log_c_level = LOG_ERROR; break;
        case X16_LOG_LEVEL_FATAL: log_c_level = LOG_FATAL; break;
        default: log_c_level = LOG_INFO; break;
    }
    
    log_set_level(log_c_level);
}

// Checkpoint-based error tracking implementation

// Helper function to add an entry to the checkpoint buffer
static void add_checkpoint_entry(const char* message, x16_log_level_t level) {
    if (!checkpoint_active || !message) {
        return;
    }
    
    // Expand buffer if needed
    if (checkpoint_count >= checkpoint_capacity) {
        int new_capacity = checkpoint_capacity == 0 ? 16 : checkpoint_capacity * 2;
        x16_log_entry_t *new_entries = realloc(checkpoint_entries, new_capacity * sizeof(x16_log_entry_t));
        if (!new_entries) {
            return; // Failed to allocate memory
        }
        checkpoint_entries = new_entries;
        checkpoint_capacity = new_capacity;
    }
    
    // Add the entry
    checkpoint_entries[checkpoint_count].message = strdup(message);
    checkpoint_entries[checkpoint_count].level = level;
    checkpoint_entries[checkpoint_count].timestamp = time(NULL);
    checkpoint_count++;
}

// Set a checkpoint for error tracking
void x16_logging_set_checkpoint(void) {
    // Clear any existing checkpoint first
    x16_logging_clear_checkpoint();
    
    checkpoint_active = true;
    checkpoint_count = 0;
    
    // Initialize with small capacity
    if (!checkpoint_entries) {
        checkpoint_capacity = 16;
        checkpoint_entries = malloc(checkpoint_capacity * sizeof(x16_log_entry_t));
    }
}

// Get all log entries since the last checkpoint
x16_log_entry_t* x16_logging_get_since_checkpoint(int* count) {
    if (!checkpoint_active || !count) {
        if (count) *count = 0;
        return NULL;
    }
    
    *count = checkpoint_count;
    
    // Return a copy of the entries so caller can safely use them
    if (checkpoint_count == 0) {
        return NULL;
    }
    
    x16_log_entry_t *result = malloc(checkpoint_count * sizeof(x16_log_entry_t));
    if (!result) {
        *count = 0;
        return NULL;
    }
    
    // Copy entries (including duplicating message strings)
    for (int i = 0; i < checkpoint_count; i++) {
        result[i].message = strdup(checkpoint_entries[i].message);
        result[i].level = checkpoint_entries[i].level;
        result[i].timestamp = checkpoint_entries[i].timestamp;
    }
    
    return result;
}

// Free log entries returned by x16_logging_get_since_checkpoint
void x16_logging_free_entries(x16_log_entry_t* entries, int count) {
    if (!entries || count <= 0) {
        return;
    }
    
    // Free message strings
    for (int i = 0; i < count; i++) {
        if (entries[i].message) {
            free(entries[i].message);
        }
    }
    
    // Free the array
    free(entries);
}

// Clear checkpoint and accumulated entries
void x16_logging_clear_checkpoint(void) {
    checkpoint_active = false;
    
    // Free all stored entries
    if (checkpoint_entries) {
        for (int i = 0; i < checkpoint_count; i++) {
            if (checkpoint_entries[i].message) {
                free(checkpoint_entries[i].message);
            }
        }
        free(checkpoint_entries);
        checkpoint_entries = NULL;
    }
    
    checkpoint_count = 0;
    checkpoint_capacity = 0;
}

// Public function to add checkpoint entry (called by logging macros)
void x16_logging_add_checkpoint_entry(const char* message, x16_log_level_t level) {
    add_checkpoint_entry(message, level);
}
