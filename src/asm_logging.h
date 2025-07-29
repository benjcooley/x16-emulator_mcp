// Commander X16 Emulator - 6502 Assembly Logging System
// Copyright (c) 2025
// Memory-mapped logging interface for 6502 assembly programs

#ifndef _ASM_LOGGING_H_
#define _ASM_LOGGING_H_

#include <stdint.h>
#include <stdbool.h>

// Memory-mapped logging addresses in IO3 range ($9F60-$9F6F)
#define ASM_LOG_PARAM1_ADDR     0x9F60  // Log parameter 1 (8-bit)
#define ASM_LOG_PARAM2_ADDR     0x9F61  // Log parameter 2 (8-bit)
#define ASM_LOG_INFO_TRIGGER    0x9F62  // Info log trigger
#define ASM_LOG_WARNING_TRIGGER 0x9F63  // Warning log trigger
#define ASM_LOG_ERROR_TRIGGER   0x9F64  // Error log trigger

// Log levels
typedef enum {
    ASM_LOG_LEVEL_INFO = 0,
    ASM_LOG_LEVEL_WARNING = 1,
    ASM_LOG_LEVEL_ERROR = 2
} asm_log_level_t;

// Log parameter storage
typedef struct {
    uint8_t param1;
    uint8_t param2;
} asm_log_params_t;

// Function prototypes
#ifdef __cplusplus
extern "C" {
#endif

void asm_logging_init(void);
void asm_logging_cleanup(void);
void asm_logging_reset(void);  // Called on NMI reset
bool asm_logging_load_definitions(void);  // Load from fixed "logging.def" file
void asm_logging_write_handler(uint16_t address, uint8_t value);
uint8_t asm_logging_read_handler(uint16_t address, bool debugOn);
void asm_logging_emit_log(asm_log_level_t level, uint8_t message_id);

#ifdef __cplusplus
}
#endif

// Internal state
extern asm_log_params_t asm_log_params;
extern bool asm_logging_enabled;

#endif // _ASM_LOGGING_H_
