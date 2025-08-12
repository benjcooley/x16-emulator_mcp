// Commander X16 Emulator - 6502 Assembly Logging System
// Copyright (c) 2025
// Memory-mapped logging interface for 6502 assembly programs

#include "asm_logging.h"

extern "C" {
#include "logging.h"
#include "log.h"
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

// Include JSON parsing
#include "extern/include/json.hpp"
using json = nlohmann::json;

// Global state
asm_log_params_t asm_log_params = {0, 0};
bool asm_logging_enabled = false;

// Log definitions storage
static json log_definitions;
static bool definitions_loaded = false;
static bool load_attempted = false;  // Track if we've tried to load since last reset
static char current_program_path[512] = {0};

// Log level names
static const char* log_level_names[] = {
    "INFO",
    "WARNING", 
    "ERROR"
};

void asm_logging_init(void) {
    asm_log_params.param1 = 0;
    asm_log_params.param2 = 0;
    asm_logging_enabled = true;
    definitions_loaded = false;
    load_attempted = false;
    memset(current_program_path, 0, sizeof(current_program_path));
    log_definitions = json::object();
    
    X16_LOG_INFO("ASM Logging: Initialized 6502 assembly logging system");
}

void asm_logging_reset(void) {
    // Called on NMI reset - clear load attempt flag to allow retry
    load_attempted = false;
    definitions_loaded = false;
    log_definitions.clear();
    
    X16_LOG_INFO("ASM Logging: Reset - will attempt to load logging.def on next access");
}

void asm_logging_cleanup(void) {
    asm_logging_enabled = false;
    definitions_loaded = false;
    log_definitions.clear();
    memset(current_program_path, 0, sizeof(current_program_path));
    
    X16_LOG_INFO("ASM Logging: Cleaned up 6502 assembly logging system");
}

bool asm_logging_load_definitions(void) {
    // Get the prg_path from main.c
    extern char *prg_path;
    
    char ldf_path[512];
    bool found_logging_file = false;
    
    // If we have a prg_path, try to construct game-specific logging filename
    if (prg_path && strlen(prg_path) > 0) {
        // Extract basename from prg_path (remove directory and .prg extension)
        const char* basename = strrchr(prg_path, '/');
        if (!basename) {
            basename = strrchr(prg_path, '\\'); // Windows path separator
        }
        if (basename) {
            basename++; // Skip the separator
        } else {
            basename = prg_path; // No path separator found
        }
        
        // Create game-specific logging filename: basename + "log.def"
        // e.g., "pacman.prg" -> "pacmanlog.def"
        strncpy(ldf_path, basename, sizeof(ldf_path) - 1);
        ldf_path[sizeof(ldf_path) - 1] = '\0';
        
        // Remove .prg extension if present
        char* dot = strrchr(ldf_path, '.');
        if (dot && strcmp(dot, ".prg") == 0) {
            *dot = '\0';
        }
        
        // Append "log.def"
        strncat(ldf_path, "log.def", sizeof(ldf_path) - strlen(ldf_path) - 1);
        
        X16_LOG_INFO("ASM Logging: Trying game-specific logging file: %s", ldf_path);
        
        // Try to load the game-specific log definition file
        SDL_RWops* file = SDL_RWFromFile(ldf_path, "r");
        if (file) {
            SDL_RWclose(file);
            found_logging_file = true;
        }
    }
    
    // If no game-specific file found, fall back to generic logging.def
    if (!found_logging_file) {
        strcpy(ldf_path, "logging.def");
        X16_LOG_INFO("ASM Logging: Falling back to generic logging file: %s", ldf_path);
    }
    
    // Try to load the log definition file
    SDL_RWops* file = SDL_RWFromFile(ldf_path, "r");
    if (!file) {
        X16_LOG_WARN("ASM Logging: No log definition file found at %s", ldf_path);
        definitions_loaded = false;
        return false;
    }
    
    // Get file size
    SDL_RWseek(file, 0, RW_SEEK_END);
    long file_size = SDL_RWtell(file);
    SDL_RWseek(file, 0, RW_SEEK_SET);
    
    if (file_size <= 0) {
        SDL_RWclose(file);
        X16_LOG_ERROR("ASM Logging: Log definition file is empty: %s", ldf_path);
        return false;
    }
    
    // Read file contents
    char* file_contents = (char*)malloc(file_size + 1);
    if (!file_contents) {
        SDL_RWclose(file);
        X16_LOG_ERROR("ASM Logging: Failed to allocate memory for log definitions");
        return false;
    }
    
    size_t bytes_read = SDL_RWread(file, file_contents, 1, file_size);
    file_contents[bytes_read] = '\0';
    SDL_RWclose(file);
    
    // Parse JSON
    try {
        log_definitions = json::parse(file_contents);
        definitions_loaded = true;
        // No program path needed for fixed filename approach
        
        X16_LOG_INFO("ASM Logging: Loaded log definitions from %s", ldf_path);
        
        // Print summary of loaded definitions
        for (auto& [level, messages] : log_definitions.items()) {
            if (messages.is_object()) {
                X16_LOG_INFO("ASM Logging: - %s: %zu messages", level.c_str(), messages.size());
            }
        }
        
    } catch (const json::exception& e) {
        X16_LOG_ERROR("ASM Logging: Failed to parse log definitions JSON: %s", e.what());
        definitions_loaded = false;
        free(file_contents);
        return false;
    }
    
    free(file_contents);
    return true;
}

void asm_logging_write_handler(uint16_t address, uint8_t value) {
    if (!asm_logging_enabled) {
        return;
    }
    
    // Debug: Log all write attempts to assembly logging addresses
    X16_LOG_INFO("ASM Logging: Write to address $%04X, value $%02X", address, value);
    
    switch (address) {
        case ASM_LOG_PARAM1_ADDR:
            asm_log_params.param1 = value;
            break;
            
        case ASM_LOG_PARAM2_ADDR:
            asm_log_params.param2 = value;
            break;
            
        case ASM_LOG_INFO_TRIGGER:
            asm_logging_emit_log(ASM_LOG_LEVEL_INFO, value);
            break;
            
        case ASM_LOG_WARNING_TRIGGER:
            asm_logging_emit_log(ASM_LOG_LEVEL_WARNING, value);
            break;
            
        case ASM_LOG_ERROR_TRIGGER:
            asm_logging_emit_log(ASM_LOG_LEVEL_ERROR, value);
            break;
            
        default:
            // Unknown address in our range
            break;
    }
}

uint8_t asm_logging_read_handler(uint16_t address, bool debugOn) {
    if (!asm_logging_enabled) {
        return 0xFF; // Open bus
    }
    
    switch (address) {
        case ASM_LOG_PARAM1_ADDR:
            return asm_log_params.param1;
            
        case ASM_LOG_PARAM2_ADDR:
            return asm_log_params.param2;
            
        case ASM_LOG_INFO_TRIGGER:
        case ASM_LOG_WARNING_TRIGGER:
        case ASM_LOG_ERROR_TRIGGER:
            // Reading trigger addresses returns the logging enabled status
            return asm_logging_enabled ? 1 : 0;
            
        default:
            return 0xFF; // Open bus
    }
}

void asm_logging_emit_log(asm_log_level_t level, uint8_t message_id) {
    if (!asm_logging_enabled) {
        return;
    }
    
    const char* level_name = (level < 3) ? log_level_names[level] : "UNKNOWN";
    
    // Lazy loading: try to load definitions on first access after reset
    if (!definitions_loaded && !load_attempted) {
        load_attempted = true;
        asm_logging_load_definitions();
    }
    
    // If no definitions loaded, just emit a basic log
    if (!definitions_loaded) {
        switch (level) {
            case ASM_LOG_LEVEL_INFO:
                X16_LOG_INFO("ASM %s: Message ID %d (params: $%02X, $%02X)", 
                           level_name, message_id, asm_log_params.param1, asm_log_params.param2);
                break;
            case ASM_LOG_LEVEL_WARNING:
                X16_LOG_WARN("ASM %s: Message ID %d (params: $%02X, $%02X)", 
                           level_name, message_id, asm_log_params.param1, asm_log_params.param2);
                break;
            case ASM_LOG_LEVEL_ERROR:
                X16_LOG_ERROR("ASM %s: Message ID %d (params: $%02X, $%02X)", 
                            level_name, message_id, asm_log_params.param1, asm_log_params.param2);
                break;
        }
        return;
    }
    
    // Look up the message in the definitions
    std::string level_key;
    switch (level) {
        case ASM_LOG_LEVEL_INFO: level_key = "info"; break;
        case ASM_LOG_LEVEL_WARNING: level_key = "warning"; break;
        case ASM_LOG_LEVEL_ERROR: level_key = "error"; break;
        default: level_key = "unknown"; break;
    }
    
    try {
        if (!log_definitions.contains(level_key)) {
            X16_LOG_WARN("ASM %s: No definitions for level '%s', Message ID %d (params: $%02X, $%02X)",
                   level_name, level_key.c_str(), message_id, asm_log_params.param1, asm_log_params.param2);
            return;
        }
        
        auto& level_messages = log_definitions[level_key];
        std::string message_key = std::to_string(message_id);
        
        if (!level_messages.contains(message_key)) {
            X16_LOG_WARN("ASM %s: No message defined for ID %d (params: $%02X, $%02X)",
                   level_name, message_id, asm_log_params.param1, asm_log_params.param2);
            return;
        }
        
        std::string message_template = level_messages[message_key];
        
        // Process the message template
        std::string formatted_message = message_template;
        
        // Replace %1 with param1 (8-bit hex)
        size_t pos = 0;
        while ((pos = formatted_message.find("%1", pos)) != std::string::npos) {
            char param_str[8];
            snprintf(param_str, sizeof(param_str), "$%02X", asm_log_params.param1);
            formatted_message.replace(pos, 2, param_str);
            pos += strlen(param_str);
        }
        
        // Replace %2 with param2 (8-bit hex)
        pos = 0;
        while ((pos = formatted_message.find("%2", pos)) != std::string::npos) {
            char param_str[8];
            snprintf(param_str, sizeof(param_str), "$%02X", asm_log_params.param2);
            formatted_message.replace(pos, 2, param_str);
            pos += strlen(param_str);
        }
        
        // Replace %3 with combined 16-bit value (param1 = low, param2 = high)
        pos = 0;
        while ((pos = formatted_message.find("%3", pos)) != std::string::npos) {
            char param_str[8];
            uint16_t combined = asm_log_params.param1 | (asm_log_params.param2 << 8);
            snprintf(param_str, sizeof(param_str), "$%04X", combined);
            formatted_message.replace(pos, 2, param_str);
            pos += strlen(param_str);
        }
        
        // Emit the formatted log message using log.c without file/line info
        switch (level) {
            case ASM_LOG_LEVEL_INFO:
                log_log(LOG_INFO, NULL, 0, "ASM %s: %s", level_name, formatted_message.c_str());
                break;
            case ASM_LOG_LEVEL_WARNING:
                log_log(LOG_WARN, NULL, 0, "ASM %s: %s", level_name, formatted_message.c_str());
                break;
            case ASM_LOG_LEVEL_ERROR:
                log_log(LOG_ERROR, NULL, 0, "ASM %s: %s", level_name, formatted_message.c_str());
                break;
        }
        
    } catch (const json::exception& e) {
        X16_LOG_ERROR("ASM %s: Error processing message ID %d: %s (params: $%02X, $%02X)",
               level_name, message_id, e.what(), asm_log_params.param1, asm_log_params.param2);
    }
}
