// Commander X16 Emulator - MCP Server Implementation
// Copyright (c) 2025
// Embedded MCP (Model Context Protocol) server for real-time emulator control
//
// IMPORTANT: httplib POST Request Requirements
// The httplib library requires POST requests to have both:
// 1. Content-Type header (e.g., "application/json")
// 2. Request body (even if empty, use "{}")
// Without these, POST requests will hit the error handler and return "Endpoint not found".

#include "mcp_server.h"
#include "keyboard_processor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <chrono>
#include <time.h>

// Include logging functions with proper C linkage
extern "C" {
#include "logging.h"
#include "x16_buffer.h"
}

// C++ RAII wrapper for safe buffer management
class X16Buffer {
private:
    x16_buffer_t buffer;
    
public:
    // Constructor - initializes empty buffer
    X16Buffer() {
        x16_buffer_init(&buffer);
    }
    
    // Constructor with initial capacity
    explicit X16Buffer(size_t initial_capacity) {
        x16_buffer_init(&buffer);
        if (!x16_buffer_alloc(&buffer, initial_capacity)) {
            throw std::runtime_error("Failed to allocate buffer");
        }
    }
    
    // Destructor - automatically frees memory
    ~X16Buffer() {
        x16_buffer_free(&buffer);
    }
    
    // Delete copy constructor and assignment to prevent double-free
    X16Buffer(const X16Buffer&) = delete;
    X16Buffer& operator=(const X16Buffer&) = delete;
    
    // Move constructor
    X16Buffer(X16Buffer&& other) noexcept {
        buffer = other.buffer;
        x16_buffer_init(&other.buffer); // Reset other to empty state
    }
    
    // Move assignment
    X16Buffer& operator=(X16Buffer&& other) noexcept {
        if (this != &other) {
            x16_buffer_free(&buffer);
            buffer = other.buffer;
            x16_buffer_init(&other.buffer);
        }
        return *this;
    }
    
    // Allocate buffer with specified capacity
    bool alloc(size_t capacity) {
        return x16_buffer_alloc(&buffer, capacity);
    }
    
    // Resize buffer
    bool resize(size_t new_capacity) {
        return x16_buffer_resize(&buffer, new_capacity);
    }
    
    // Set buffer contents
    bool set(const char* data, size_t len) {
        return x16_buffer_set(&buffer, data, len);
    }
    
    // Set buffer contents from std::string
    bool set(const std::string& str) {
        return x16_buffer_set(&buffer, str.c_str(), str.length());
    }
    
    // Append data to buffer
    bool append(const char* data, size_t len) {
        return x16_buffer_append(&buffer, data, len);
    }
    
    // Append std::string to buffer
    bool append(const std::string& str) {
        return x16_buffer_append(&buffer, str.c_str(), str.length());
    }
    
    // Clear buffer contents
    void clear() {
        x16_buffer_clear(&buffer);
    }
    
    // Get raw buffer pointer (for C function calls)
    x16_buffer_t* get() {
        return &buffer;
    }
    
    // Get const raw buffer pointer
    const x16_buffer_t* get() const {
        return &buffer;
    }
    
    // Get data pointer
    const char* data() const {
        return buffer.data;
    }
    
    // Get current size
    size_t size() const {
        return buffer.size;
    }
    
    // Get capacity
    size_t capacity() const {
        return buffer.capacity;
    }
    
    // Check if buffer is empty
    bool empty() const {
        return buffer.size == 0;
    }
    
    // Convert to std::string (ensures null termination)
    std::string to_string() const {
        if (!buffer.data || buffer.size == 0) {
            return std::string();
        }
        return std::string(buffer.data, buffer.size);
    }
    
    // Ensure null termination
    bool ensure_null_terminated() {
        return x16_buffer_ensure_null_terminated(&buffer);
    }
};

// Forward declarations for screenshot functions
extern "C" bool video_take_screenshot(void);
extern "C" const char* get_last_screenshot_filename(void);

// Include httplib.h for HTTP server functionality
#define HTTPLIB_IMPLEMENTATION
#include "extern/include/httplib.h"
#include "extern/include/json.hpp"

using json = nlohmann::json;

// External emulator functions we need to access
extern "C" {
    extern void machine_reset(void);
    extern void machine_nmi(void);
    extern bool mcp_enabled;
    extern int mcp_port;
    extern bool mcp_debug;
    
    // CPU state access
    extern struct regs regs;
    extern uint32_t clockticks6502;
    extern uint32_t instructions;
    
    // Memory state access
    extern uint8_t memory_get_ram_bank(void);
    extern uint8_t memory_get_rom_bank(void);
    extern uint16_t num_ram_banks;
    
    // Video/VERA state access
    extern uint8_t video_read(uint8_t reg, bool debugOn);
    extern uint32_t video_get_address(uint8_t sel);
    extern uint8_t video_get_dc_value(uint8_t reg);
    
    // Keyboard input functions
    extern void keyboard_add_event(uint8_t key, bool pressed);
    extern void keyboard_add_char(char c);
    extern bool keyboard_add_text(const char* text);
    extern int keyboard_get_queue_size(void);
    
    // Pause/unpause functions
    extern bool emulator_is_paused(void);
    extern void emulator_pause(void);
    extern void emulator_unpause(void);
}

// CPU register structure (from registers.h)
struct regs {
    union { uint16_t c; struct { uint8_t a, b; }; };
    union { uint16_t x; struct { uint8_t xl, xh; }; };
    union { uint16_t y; struct { uint8_t yl, yh; }; };
    uint16_t dp;
    uint16_t sp;
    uint8_t db;
    uint16_t pc;
    uint8_t k;
    uint8_t status;
    uint8_t e;
    bool is65c816;
};

// Global MCP server state
static mcp_server_state_t g_mcp_state = {0};
static pthread_t mcp_thread;
static httplib::Server *mcp_http_server = nullptr;

// Forward declarations
static void* mcp_server_thread(void* arg);
static void setup_mcp_routes(httplib::Server& server);

// Initialize the MCP server
bool mcp_server_init(int port, bool debug_mode) {
    if (g_mcp_state.initialized) {
        return false; // Already initialized
    }
    
    g_mcp_state.config.port = port;
    g_mcp_state.config.enabled = true;
    g_mcp_state.config.debug_mode = debug_mode;
    g_mcp_state.running = false;
    g_mcp_state.initialized = true;
    
    if (debug_mode) {
        printf("MCP Server: Initialized on port %d with debug mode enabled\n", port);
    }
    
    return true;
}

// Start the MCP server (non-blocking)
bool mcp_server_start(void) {
    if (!g_mcp_state.initialized || g_mcp_state.running) {
        return false;
    }
    
    // Create the HTTP server thread
    int result = pthread_create(&mcp_thread, NULL, mcp_server_thread, NULL);
    if (result != 0) {
        printf("MCP Server: Failed to create server thread\n");
        return false;
    }
    
    g_mcp_state.running = true;
    g_mcp_state.server_thread = &mcp_thread;
    
    if (g_mcp_state.config.debug_mode) {
        printf("MCP Server: Started on port %d\n", g_mcp_state.config.port);
    }
    
    return true;
}

// Stop the MCP server
void mcp_server_stop(void) {
    if (!g_mcp_state.running) {
        return;
    }
    
    g_mcp_state.running = false;
    
    if (mcp_http_server) {
        mcp_http_server->stop();
    }
    
    if (g_mcp_state.server_thread) {
        pthread_join(mcp_thread, NULL);
        g_mcp_state.server_thread = NULL;
    }
    
    if (g_mcp_state.config.debug_mode) {
        printf("MCP Server: Stopped\n");
    }
}

// Cleanup MCP server resources
void mcp_server_cleanup(void) {
    mcp_server_stop();
    
    if (mcp_http_server) {
        delete mcp_http_server;
        mcp_http_server = nullptr;
    }
    
    g_mcp_state.initialized = false;
    
    if (g_mcp_state.config.debug_mode) {
        printf("MCP Server: Cleaned up\n");
    }
}

// Check if MCP server is running
bool mcp_server_is_running(void) {
    return g_mcp_state.running;
}

// Get current server state
mcp_server_state_t* mcp_server_get_state(void) {
    return &g_mcp_state;
}

// MCP server thread function
static void* mcp_server_thread(void* arg) {
    (void)arg; // Unused parameter
    
    mcp_http_server = new httplib::Server();
    
    if (!mcp_http_server) {
        printf("MCP Server: Failed to create HTTP server\n");
        g_mcp_state.running = false;
        return NULL;
    }
    
    // Set up MCP routes
    setup_mcp_routes(*mcp_http_server);
    
    // Start the server
    if (g_mcp_state.config.debug_mode) {
        printf("MCP Server: HTTP server listening on port %d\n", g_mcp_state.config.port);
    }
    
    bool success = mcp_http_server->listen("127.0.0.1", g_mcp_state.config.port);
    
    if (!success && g_mcp_state.running) {
        printf("MCP Server: Failed to start HTTP server on port %d\n", g_mcp_state.config.port);
    }
    
    g_mcp_state.running = false;
    return NULL;
}

// Set up MCP HTTP routes
static void setup_mcp_routes(httplib::Server& server) {
    if (g_mcp_state.config.debug_mode) {
        printf("MCP Server: Setting up routes\n");
    }
    
    
    // Root endpoint - MCP server info
    server.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        json response = {
            {"name", "x16-emulator-mcp-server"},
            {"version", "0.1.0"},
            {"description", "Embedded MCP server for Commander X16 Emulator"},
            {"endpoints", {
                "GET / - Server info",
                "POST /reset - Reset emulator",
                "POST /nmi - Send NMI interrupt",
                "POST /screenshot - Capture screenshot only",
                "POST /snapshot - Capture system state (CPU, memory, VERA) with screenshot",
                "POST /shutdown - Shutdown emulator",
                "POST /restart - Restart emulator",
                "POST /test - Test endpoint",
                "GET /status - Get server status",
                "GET /reset-get - Reset via GET (testing)",
                "POST /keyboard - Send keyboard input with macro support",
                "POST /joystick - Send joystick input commands"
            }},
            {"note", "MCP tools: screenshot=image only, snapshot=system state+image"}
        };
        res.set_content(response.dump(), "application/json");
    });
    
    // Simple test POST endpoint
    server.Post("/test", [](const httplib::Request& req, httplib::Response& res) {
        std::string response = R"({"status": "success", "message": "Test POST endpoint working"})";
        res.set_content(response, "application/json");
    });
    
    // Reset the emulator
    server.Post("/reset", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Reset command received\n");
        }
        
        machine_reset();
        
        std::string response = R"({"status": "success"})";
        res.set_content(response, "application/json");
        return;
    });
    
    // Send NMI to the emulator
    server.Post("/nmi", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: NMI command received\n");
        }
        
        machine_nmi();
        
        std::string response = R"({"status": "success"})";
        res.set_content(response, "application/json");
        return;
    });
    
    // Take a screenshot
    server.Post("/screenshot", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Screenshot command received\n");
        }
        
        // Set checkpoint to capture any errors during screenshot operation
        x16_logging_set_checkpoint();
        
        // Initialize filename buffer to avoid garbage data
        char filename_buffer[512];
        memset(filename_buffer, 0, sizeof(filename_buffer));
        
        // Log checkpoint for error tracking
        log_info("MCP Server: Starting screenshot capture");
        
        // Take screenshot using new API
        bool success = video_take_screenshot();
        
        if (success) {
            const char* filename = get_last_screenshot_filename();
            if (filename && strlen(filename) > 0) {
                // Return path that only needs x16:// prefixed
                std::string path = "screenshot/" + std::string(filename);
                
                // Create JSON response with path
                json response = {
                    {"status", "success"},
                    {"path", path}
                };
                
                log_info("MCP Server: Screenshot captured successfully: %s", filename);
                
                // Clear checkpoint on success
                x16_logging_clear_checkpoint();
                
                res.set_content(response.dump(), "application/json");
            } else {
                json response = {
                    {"status", "error"},
                    {"message", "Screenshot taken but filename is null or empty"}
                };
                res.set_content(response.dump(), "application/json");
            }
        } else {
            // Get all errors that occurred since checkpoint
            int error_count = 0;
            x16_log_entry_t* errors = x16_logging_get_since_checkpoint(&error_count);
            
            std::string error_message;
            if (!success) {
                error_message = "video_take_screenshot() returned false";
                if (strlen(filename_buffer) == 0) {
                    error_message += " - filename buffer is empty, function failed before setting filename";
                } else {
                    error_message += " - filename buffer contains: \"" + std::string(filename_buffer) + "\"";
                }
            } else {
                error_message = "video_take_screenshot() returned true but filename is empty";
            }
            
            // Append any logged errors from the call stack
            if (errors && error_count > 0) {
                error_message += " - Logged errors: ";
                for (int i = 0; i < error_count; i++) {
                    if (i > 0) error_message += "; ";
                    error_message += errors[i].message;
                }
                x16_logging_free_entries(errors, error_count);
            }
            
            json response = {
                {"status", "error"},
                {"message", error_message}
            };
            
            log_error("MCP Server: Screenshot failed: %s", error_message.c_str());
            
            // Clear checkpoint after processing errors
            x16_logging_clear_checkpoint();
            
            res.set_content(response.dump(), "application/json");
        }
    });
    
    // Get server status
    server.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        json response = {
            {"status", "running"},
            {"port", g_mcp_state.config.port},
            {"debug", g_mcp_state.config.debug_mode}
        };
        res.set_content(response.dump(), "application/json");
    });
    
    // Shutdown the emulator
    server.Post("/shutdown", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Shutdown command received\n");
        }
        
        std::string response = R"({"status": "success", "message": "Emulator shutting down"})";
        res.set_content(response, "application/json");
        
        // Schedule shutdown after response is sent
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            exit(0);
        }).detach();
    });
    
    // Take a system snapshot
    server.Post("/snapshot", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Snapshot command received\n");
        }
        
        // Set checkpoint to capture any errors during snapshot operation
        x16_logging_set_checkpoint();
        
        // Initialize filename buffer to avoid garbage data
        char filename_buffer[512];
        memset(filename_buffer, 0, sizeof(filename_buffer));
        
        // Log checkpoint for error tracking
        log_info("MCP Server: Starting snapshot capture");
        
        // Take screenshot using new API
        bool success = video_take_screenshot();
        
        if (success) {
            const char* filename = get_last_screenshot_filename();
            if (filename && strlen(filename) > 0) {
                // Return path that only needs x16:// prefixed
                std::string path = "screenshot/" + std::string(filename);
                
                // Capture real CPU state
            char pc_str[8], a_str[8], x_str[8], y_str[8], sp_str[8], flags_str[8];
            char dp_str[8], db_str[8], k_str[8];
            snprintf(pc_str, sizeof(pc_str), "0x%04X", regs.pc);
            snprintf(a_str, sizeof(a_str), "0x%02X", regs.a);
            snprintf(x_str, sizeof(x_str), "0x%02X", regs.xl);
            snprintf(y_str, sizeof(y_str), "0x%02X", regs.yl);
            snprintf(sp_str, sizeof(sp_str), "0x%04X", regs.sp);
            snprintf(flags_str, sizeof(flags_str), "0x%02X", regs.status);
            snprintf(dp_str, sizeof(dp_str), "0x%04X", regs.dp);
            snprintf(db_str, sizeof(db_str), "0x%02X", regs.db);
            snprintf(k_str, sizeof(k_str), "0x%02X", regs.k);
            
            // Capture memory state
            uint8_t current_ram_bank = memory_get_ram_bank();
            uint8_t current_rom_bank = memory_get_rom_bank();
            
            // Capture VERA state - key registers
            char vera_addr0_str[8], vera_addr1_str[8];
            uint32_t vera_addr0 = video_get_address(0);
            uint32_t vera_addr1 = video_get_address(1);
            snprintf(vera_addr0_str, sizeof(vera_addr0_str), "0x%05X", vera_addr0);
            snprintf(vera_addr1_str, sizeof(vera_addr1_str), "0x%05X", vera_addr1);
            
            // Read key VERA registers (using debug mode to avoid side effects)
            uint8_t vera_ctrl = video_read(0x00, true);  // VERA_CTRL
            uint8_t vera_ien = video_read(0x01, true);   // VERA_IEN
            uint8_t vera_isr = video_read(0x02, true);   // VERA_ISR
            uint8_t vera_dc_video = video_read(0x05, true); // DC_VIDEO
            
            char vera_ctrl_str[8], vera_ien_str[8], vera_isr_str[8], vera_dc_video_str[8];
            snprintf(vera_ctrl_str, sizeof(vera_ctrl_str), "0x%02X", vera_ctrl);
            snprintf(vera_ien_str, sizeof(vera_ien_str), "0x%02X", vera_ien);
            snprintf(vera_isr_str, sizeof(vera_isr_str), "0x%02X", vera_isr);
            snprintf(vera_dc_video_str, sizeof(vera_dc_video_str), "0x%02X", vera_dc_video);

            // Create JSON response with path and real snapshot data (without base64 screenshot)
            json response = {
                {"status", "success"},
                {"path", path},
                {"data", {
                    {"cpu", {
                        {"pc", pc_str},
                        {"a", a_str},
                        {"x", x_str},
                        {"y", y_str},
                        {"sp", sp_str},
                        {"flags", flags_str},
                        {"dp", dp_str},
                        {"db", db_str},
                        {"k", k_str},
                        {"is_65c816", regs.is65c816},
                        {"emulation_mode", regs.e != 0},
                        {"clock_ticks", clockticks6502},
                        {"instructions", instructions}
                    }},
                    {"memory", {
                        {"ram_banks_total", num_ram_banks},
                        {"current_ram_bank", current_ram_bank},
                        {"current_rom_bank", current_rom_bank}
                    }},
                    {"vera", {
                        {"ctrl", vera_ctrl_str},
                        {"ien", vera_ien_str},
                        {"isr", vera_isr_str},
                        {"dc_video", vera_dc_video_str},
                        {"addr0", vera_addr0_str},
                        {"addr1", vera_addr1_str}
                    }}
                }}
            };
            log_info("MCP Server: Snapshot captured successfully: %s", filename);
            
            // Clear checkpoint on success
            x16_logging_clear_checkpoint();
            
            res.set_content(response.dump(), "application/json");
            } else {
                json response = {
                    {"status", "error"},
                    {"message", "Screenshot taken but filename is null or empty"}
                };
                res.set_content(response.dump(), "application/json");
            }
        } else {
            // Get all errors that occurred since checkpoint
            int error_count = 0;
            x16_log_entry_t* errors = x16_logging_get_since_checkpoint(&error_count);
            
            std::string error_message;
            if (!success) {
                error_message = "video_take_screenshot() returned false";
                if (strlen(filename_buffer) == 0) {
                    error_message += " - filename buffer is empty, function failed before setting filename";
                } else {
                    error_message += " - filename buffer contains: \"" + std::string(filename_buffer) + "\"";
                }
            } else {
                error_message = "video_take_screenshot() returned true but filename is empty";
            }
            
            // Append any logged errors from the call stack
            if (errors && error_count > 0) {
                error_message += " - Logged errors: ";
                for (int i = 0; i < error_count; i++) {
                    if (i > 0) error_message += "; ";
                    error_message += errors[i].message;
                }
                x16_logging_free_entries(errors, error_count);
            }
            
            json response = {
                {"status", "error"},
                {"message", error_message}
            };
            
            log_error("MCP Server: Snapshot screenshot failed: %s", error_message.c_str());
            
            // Clear checkpoint after processing errors
            x16_logging_clear_checkpoint();
            
            res.set_content(response.dump(), "application/json");
        }
    });
    
    // Restart the emulator (for now, just reset)
    server.Post("/restart", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Restart command received\n");
        }
        
        machine_reset();
        
        std::string response = R"({"status": "success", "message": "Emulator restarted"})";
        res.set_content(response, "application/json");
    });
    
    // Send keyboard input to the emulator
    server.Post("/keyboard", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Keyboard command received\n");
        }
        
        try {
            json request_json = json::parse(req.body);
            
            if (request_json.contains("text")) {
                // STEP 1: Test basic keyboard input without new processing
                std::string text = request_json["text"];
                
                // Get current queue size before adding text
                int queue_size_before = keyboard_get_queue_size();
                
                // Use the original working keyboard_add_text function
                bool success = keyboard_add_text(text.c_str());
                
                if (success) {
                    // Calculate timing information
                    int text_length = text.length();
                    int estimated_time_ms = text_length * 10; // 10ms per character
                    double estimated_time_seconds = estimated_time_ms / 1000.0;
                    
                    // Get queue size after adding text
                    int queue_size_after = keyboard_get_queue_size();
                    int total_queue_time_ms = queue_size_after * 10;
                    double total_queue_time_seconds = total_queue_time_ms / 1000.0;
                    
                    json response = {
                        {"status", "success"},
                        {"message", "Text queued for emulator (original method)"},
                        {"text", text},
                        {"characters", text_length},
                        {"estimated_time_ms", estimated_time_ms},
                        {"estimated_time_seconds", estimated_time_seconds},
                        {"typing_rate_ms_per_char", 10},
                        {"queue_info", {
                            {"size_before", queue_size_before},
                            {"size_after", queue_size_after},
                            {"total_queue_time_ms", total_queue_time_ms},
                            {"total_queue_time_seconds", total_queue_time_seconds}
                        }}
                    };
                    res.set_content(response.dump(), "application/json");
                } else {
                    json response = {
                        {"status", "error"},
                        {"message", "Failed to queue text - buffer may be full"},
                        {"text", text},
                        {"characters", text.length()},
                        {"queue_size", keyboard_get_queue_size()}
                    };
                    res.set_content(response.dump(), "application/json");
                }
                
                /* COMMENTED OUT: New processing system for debugging
                // Process text with enhanced macro support and pauses
                std::string mode_str = request_json.value("mode", "ascii"); // Default to ASCII mode
                KeyboardMode mode = string_to_keyboard_mode(mode_str);
                
                // Use the enhanced keyboard processor
                ProcessedKeyboardData result = process_keyboard_input(text, mode);
                
                if (result.success) {
                    // Queue the processed keycodes
                    // TODO: Implement proper pause handling in the keyboard queue
                    bool success = true;
                    for (uint8_t keycode : result.keycodes) {
                        keyboard_add_event(keycode, true);  // Press
                        keyboard_add_event(keycode, false); // Release
                    }
                    
                    if (success) {
                        int queue_size_after = keyboard_get_queue_size();
                        
                        json response = {
                            {"status", "success"},
                            {"message", "Text processed and queued for emulator"},
                            {"text", text},
                            {"mode", mode_str},
                            {"characters", result.total_characters},
                            {"total_pause_time_ms", result.total_pause_time_ms},
                            {"pause_count", result.pause_durations.size()},
                            {"estimated_time_ms", result.total_characters * 20 + result.total_pause_time_ms}, // 20ms per char (press+release)
                            {"queue_info", {
                                {"size_before", queue_size_before},
                                {"size_after", queue_size_after}
                            }}
                        };
                        res.set_content(response.dump(), "application/json");
                    } else {
                        json response = {
                            {"status", "error"},
                            {"message", "Failed to queue processed text - buffer may be full"},
                            {"text", text},
                            {"characters", result.total_characters}
                        };
                        res.set_content(response.dump(), "application/json");
                    }
                } else {
                    json response = {
                        {"status", "error"},
                        {"message", result.error_message},
                        {"text", text},
                        {"mode", mode_str}
                    };
                    res.set_content(response.dump(), "application/json");
                }
                */
                
            } else if (request_json.contains("key")) {
                // Send single key or special key
                std::string key_str = request_json["key"];
                bool pressed = request_json.value("pressed", true); // Default to key press
                
                // Handle special keys
                uint8_t key_code = 0;
                if (key_str == "ENTER" || key_str == "enter") {
                    key_code = 13; // CR
                } else if (key_str == "ESCAPE" || key_str == "escape") {
                    key_code = 27; // ESC
                } else if (key_str == "BACKSPACE" || key_str == "backspace") {
                    key_code = 8; // BS
                } else if (key_str == "TAB" || key_str == "tab") {
                    key_code = 9; // TAB
                } else if (key_str == "SPACE" || key_str == "space") {
                    key_code = 32; // Space
                } else if (key_str == "UP" || key_str == "up") {
                    key_code = 145; // Cursor up
                } else if (key_str == "DOWN" || key_str == "down") {
                    key_code = 17; // Cursor down
                } else if (key_str == "LEFT" || key_str == "left") {
                    key_code = 157; // Cursor left
                } else if (key_str == "RIGHT" || key_str == "right") {
                    key_code = 29; // Cursor right
                } else if (key_str == "F1" || key_str == "f1") {
                    key_code = 133; // F1
                } else if (key_str == "F2" || key_str == "f2") {
                    key_code = 137; // F2
                } else if (key_str == "F3" || key_str == "f3") {
                    key_code = 134; // F3
                } else if (key_str == "F4" || key_str == "f4") {
                    key_code = 138; // F4
                } else if (key_str == "F5" || key_str == "f5") {
                    key_code = 135; // F5
                } else if (key_str == "F6" || key_str == "f6") {
                    key_code = 139; // F6
                } else if (key_str == "F7" || key_str == "f7") {
                    key_code = 136; // F7
                } else if (key_str == "F8" || key_str == "f8") {
                    key_code = 140; // F8
                } else if (key_str.length() == 1) {
                    // Single ASCII character
                    key_code = (uint8_t)key_str[0];
                } else {
                    // Invalid key
                    json response = {
                        {"status", "error"},
                        {"message", "Invalid key: " + key_str}
                    };
                    res.set_content(response.dump(), "application/json");
                    return;
                }
                
                keyboard_add_event(key_code, pressed);
                
                json response = {
                    {"status", "success"},
                    {"message", "Key sent to emulator"},
                    {"key", key_str},
                    {"pressed", pressed},
                    {"key_code", key_code}
                };
                res.set_content(response.dump(), "application/json");
                
            } else {
                json response = {
                    {"status", "error"},
                    {"message", "Missing 'text' or 'key' parameter"}
                };
                res.set_content(response.dump(), "application/json");
            }
            
        } catch (const json::exception& e) {
            json response = {
                {"status", "error"},
                {"message", "Invalid JSON: " + std::string(e.what())}
            };
            res.set_content(response.dump(), "application/json");
        }
    });
    
    // Send joystick input to the emulator
    server.Post("/joystick", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Joystick command received\n");
        }
        
        try {
            json request_json = json::parse(req.body);
            
            if (request_json.contains("commands")) {
                // Process whitespace-delimited joystick commands
                std::string commands = request_json["commands"];
                int joystick_num = request_json.value("joystick", 1); // Default to joystick 1
                
                // Use the joystick processor
                ProcessedKeyboardData result = process_joystick_input(commands, joystick_num);
                
                if (result.success) {
                    // Queue the processed joystick commands
                    // For now, we'll use the keyboard queue but with special encoding
                    // TODO: Implement proper joystick input queuing
                    
                    json response = {
                        {"status", "success"},
                        {"message", "Joystick commands processed"},
                        {"commands", commands},
                        {"joystick", joystick_num},
                        {"total_commands", result.total_characters},
                        {"total_pause_time_ms", result.total_pause_time_ms},
                        {"pause_count", result.pause_durations.size()}
                    };
                    res.set_content(response.dump(), "application/json");
                } else {
                    json response = {
                        {"status", "error"},
                        {"message", result.error_message},
                        {"commands", commands},
                        {"joystick", joystick_num}
                    };
                    res.set_content(response.dump(), "application/json");
                }
                
            } else {
                json response = {
                    {"status", "error"},
                    {"message", "Missing 'commands' parameter"}
                };
                res.set_content(response.dump(), "application/json");
            }
            
        } catch (const json::exception& e) {
            json response = {
                {"status", "error"},
                {"message", "Invalid JSON: " + std::string(e.what())}
            };
            res.set_content(response.dump(), "application/json");
        }
    });
    
    // Pause/unpause the emulator
    server.Post("/pause", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Pause command received\n");
        }
        
        try {
            json request_json = json::parse(req.body);
            bool pause_state = request_json.value("pause", true); // Default to pause
            
            if (pause_state) {
                emulator_pause();
            } else {
                emulator_unpause();
            }
            
            json response = {
                {"status", "success"},
                {"message", pause_state ? "Emulator paused" : "Emulator unpaused"},
                {"paused", emulator_is_paused()}
            };
            res.set_content(response.dump(), "application/json");
            
        } catch (const json::exception& e) {
            // If no JSON body, default to toggle pause state
            bool current_paused = emulator_is_paused();
            if (current_paused) {
                emulator_unpause();
            } else {
                emulator_pause();
            }
            
            json response = {
                {"status", "success"},
                {"message", current_paused ? "Emulator unpaused" : "Emulator paused"},
                {"paused", emulator_is_paused()}
            };
            res.set_content(response.dump(), "application/json");
        }
    });

    // GET endpoint for reset (to test if macOS is blocking POST)
    server.Get("/reset-get", [](const httplib::Request&, httplib::Response& res) {
        if (g_mcp_state.config.debug_mode) {
            printf("MCP Server: Reset command received via GET\n");
        }
        
        machine_reset();
        
        std::string response = R"({"status": "success", "message": "Emulator reset via GET"})";
        res.set_content(response, "application/json");
    });
    
    // Error handler
    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        std::string response = R"({"status": "error", "message": "Endpoint not found"})";
        res.set_content(response, "application/json");
    });
}

// C function implementations that need to be exported
extern "C" {

char* mcp_capture_screenshot_base64(void) {
    // TODO: Implement actual screenshot capture
    return strdup("placeholder_screenshot_data");
}

char* mcp_get_vera_state_json(void) {
    // TODO: Implement VERA state capture
    return strdup(R"({"vera_state": "placeholder"})");
}

char* mcp_read_memory_json(uint32_t address, uint32_t length, uint8_t bank) {
    // TODO: Implement memory reading
    char* result = (char*)malloc(256);
    snprintf(result, 256, R"({"address": %u, "length": %u, "bank": %u, "data": "placeholder"})", address, length, bank);
    return result;
}

char* mcp_get_cpu_state_json(void) {
    // TODO: Implement CPU state capture
    return strdup(R"({"cpu_state": "placeholder"})");
}

bool mcp_load_program(const char* prg_data, size_t data_size) {
    // TODO: Implement program loading
    (void)prg_data;
    (void)data_size;
    return false;
}

} // extern "C"
