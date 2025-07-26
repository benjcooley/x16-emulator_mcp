// Commander X16 Emulator - MCP Server
// Copyright (c) 2025
// Embedded MCP (Model Context Protocol) server for real-time emulator control

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// MCP Server configuration
typedef struct {
    int port;
    bool enabled;
    bool debug_mode;
} mcp_server_config_t;

// MCP Server state
typedef struct {
    bool running;
    bool initialized;
    void* server_thread;
    mcp_server_config_t config;
} mcp_server_state_t;

// Initialize the MCP server
bool mcp_server_init(int port, bool debug_mode);

// Start the MCP server (non-blocking)
bool mcp_server_start(void);

// Stop the MCP server
void mcp_server_stop(void);

// Cleanup MCP server resources
void mcp_server_cleanup(void);

// Check if MCP server is running
bool mcp_server_is_running(void);

// Get current server state
mcp_server_state_t* mcp_server_get_state(void);

// Screenshot capture function (returns base64 encoded PNG)
char* mcp_capture_screenshot_base64(void);

// VERA state capture
char* mcp_get_vera_state_json(void);

// Memory read function
char* mcp_read_memory_json(uint32_t address, uint32_t length, uint8_t bank);

// CPU state capture
char* mcp_get_cpu_state_json(void);

// Program loading function
bool mcp_load_program(const char* prg_data, size_t data_size);

#ifdef __cplusplus
}
#endif

#endif // MCP_SERVER_H
