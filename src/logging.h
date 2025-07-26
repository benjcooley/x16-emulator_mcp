/**
 * X16 Emulator Logging System
 * 
 * This header provides a macro-based logging system that redirects
 * printf/fprintf calls to the log.c library for better debugging.
 * 
 * Usage: Include this header in source files that need logging.
 * Logging is enabled by default. Define DISABLE_X16_LOGGING to disable it.
 */

#ifndef X16_LOGGING_H
#define X16_LOGGING_H

#include "log.h"
#include <stdio.h>

// Enable logging by default unless explicitly disabled
#ifndef DISABLE_X16_LOGGING
#define ENABLE_X16_LOGGING
#endif

// Log levels
typedef enum {
    X16_LOG_LEVEL_TRACE = 0,
    X16_LOG_LEVEL_DEBUG = 1,
    X16_LOG_LEVEL_INFO = 2,
    X16_LOG_LEVEL_WARN = 3,
    X16_LOG_LEVEL_ERROR = 4,
    X16_LOG_LEVEL_FATAL = 5
} x16_log_level_t;

// Global logging initialization flag
extern int x16_logging_initialized;

// Initialize the X16 logging system
void x16_logging_init(FILE *log_file);

// Cleanup logging system
void x16_logging_cleanup(void);

// Set log level
void x16_logging_set_level(x16_log_level_t level);

// Set log file
void x16_logging_set_file(const char *filename);

// Get string representation of log level
const char* x16_log_level_string(x16_log_level_t level);

// Parse log level from string
x16_log_level_t x16_parse_log_level(const char *level_str);

// Checkpoint-based error tracking
typedef struct {
    char* message;
    x16_log_level_t level;
    time_t timestamp;
} x16_log_entry_t;

// Set a checkpoint for error tracking
void x16_logging_set_checkpoint(void);

// Get all log entries since the last checkpoint
x16_log_entry_t* x16_logging_get_since_checkpoint(int* count);

// Free log entries returned by x16_logging_get_since_checkpoint
void x16_logging_free_entries(x16_log_entry_t* entries, int count);

// Clear checkpoint and accumulated entries
void x16_logging_clear_checkpoint(void);

// Internal function to add checkpoint entry (called by logging macros)
void x16_logging_add_checkpoint_entry(const char* message, x16_log_level_t level);

// Prevent macro redefinition in logging.c itself
#if defined(ENABLE_X16_LOGGING) && !defined(X16_LOGGING_IMPLEMENTATION)

// Macro to ensure logging is initialized
#define X16_LOG_INIT_CHECK() \
    do { \
        if (!x16_logging_initialized) { \
            x16_logging_init(NULL); \
        } \
    } while(0)

// Redirect printf to log_info with initialization check
#define printf(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_info(__VA_ARGS__); \
    } while(0)

// Redirect fprintf - stderr goes to log_error, others to log_info
#define fprintf(stream, ...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        if ((stream) == stderr) { \
            log_error(__VA_ARGS__); \
        } else { \
            log_info(__VA_ARGS__); \
        } \
    } while(0)

// Additional logging macros for explicit use
#define X16_LOG_TRACE(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_trace(__VA_ARGS__); \
    } while(0)

#define X16_LOG_DEBUG(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_debug(__VA_ARGS__); \
    } while(0)

#define X16_LOG_INFO(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_info(__VA_ARGS__); \
    } while(0)

#define X16_LOG_WARN(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_warn(__VA_ARGS__); \
    } while(0)

#define X16_LOG_ERROR(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_error(__VA_ARGS__); \
    } while(0)

#define X16_LOG_FATAL(...) \
    do { \
        X16_LOG_INIT_CHECK(); \
        log_fatal(__VA_ARGS__); \
    } while(0)

#else

// When logging is disabled, keep original behavior
#define X16_LOG_TRACE(...) do {} while(0)
#define X16_LOG_DEBUG(...) do {} while(0)
#define X16_LOG_INFO(...) do {} while(0)
#define X16_LOG_WARN(...) do {} while(0)
#define X16_LOG_ERROR(...) do {} while(0)
#define X16_LOG_FATAL(...) do {} while(0)

#endif // defined(ENABLE_X16_LOGGING) && !defined(X16_LOGGING_IMPLEMENTATION)

// When logging is disabled, initialize/cleanup functions do nothing
#if !defined(ENABLE_X16_LOGGING) && !defined(X16_LOGGING_IMPLEMENTATION)
#define x16_logging_init(file) do {} while(0)
#define x16_logging_cleanup() do {} while(0)
#endif

#endif // X16_LOGGING_H
