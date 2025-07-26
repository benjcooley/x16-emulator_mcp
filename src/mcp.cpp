// Commander X16 Emulator - MCP Server
// Cross-platform Model Context Protocol server for emulator control
// Copyright (c) 2025
//
// IMPORTANT: httplib POST Request Requirements
// All POST requests to the emulator's HTTP server MUST include:
// 1. Content-Type header: "application/json"
// 2. Request body (even if empty): "{}"
// Without these, POST requests will fail with "Endpoint not found" error.
// Example: http_client->Post("/screenshot", "{}", "application/json");
//
// CRITICAL: MCP Protocol Debug Output Rules
// The Model Context Protocol (MCP) requires clean JSON-RPC communication on stdout.
// ANY debug output to stderr during MCP protocol initialization will break the handshake
// and cause the MCP client to fail with "error starting the server".
//
// Rules for debug output:
// 1. NEVER output debug messages during constructor or listResources() without debug_mode check
// 2. ALL debug output must go to stderr, NEVER stdout
// 3. Debug output during MCP protocol handshake will break client connection
// 4. Only output debug messages when debug_mode is explicitly enabled via X16_DEBUG=1
// 5. Be especially careful with any output during server startup or resource listing
//
// If you see "error starting the server" from MCP client, check for:
// - Debug output during constructor
// - Debug output in listResources() method
// - Any stderr output during MCP protocol initialization

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>
#include <filesystem>
#include <regex>
#include <mutex>
#include <iomanip>

// Single-file libraries
#define HTTPLIB_IMPLEMENTATION
#include "extern/include/httplib.h"
#include "extern/include/json.hpp"

using json = nlohmann::json;

// Cross-platform compatibility
#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define EMULATOR_EXECUTABLE "x16emu.exe"
    #define NULL_DEVICE "NUL"
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <signal.h>
    #include <cerrno>
    #include <cstring>
    #define EMULATOR_EXECUTABLE "x16emu"
    #define NULL_DEVICE "/dev/null"
#endif

class X16EmulatorMCP {
private:
    int emulator_port;
    bool debug_mode;
    std::string emulator_path;
    std::string executable_dir;
    bool emulator_running;
    
    // HTTP client for communicating with emulator
    httplib::Client* http_client;
    
    // Logging system
    enum LogLevel { ERROR = 0, INFO = 1, DEBUG = 2 };
    LogLevel log_level;
    std::string log_file_path;
    std::mutex log_mutex;
    bool logging_enabled;
    
    // Get the directory where this executable is located using argv[0]
    std::string getExecutableDirectory(const char* argv0) {
        try {
            std::filesystem::path exe_path(argv0);
            
            // If argv[0] is relative, resolve it against current working directory
            if (exe_path.is_relative()) {
                exe_path = std::filesystem::current_path() / exe_path;
            }
            
            // Get the parent directory and canonicalize it
            std::filesystem::path dir_path = exe_path.parent_path();
            return std::filesystem::canonical(dir_path).string();
            
        } catch (const std::exception&) {
            // Fallback to current directory
            return ".";
        }
    }
    
public:
    X16EmulatorMCP(const char* argv0) : emulator_port(9090), debug_mode(false), emulator_running(false), http_client(nullptr), log_level(INFO), logging_enabled(false) {
        // Get configuration from environment first (so we can use debug_mode)
        const char* debug_env = std::getenv("X16_DEBUG");
        if (debug_env && std::string(debug_env) == "1") {
            debug_mode = true;
        }
        
        // Initialize logging system
        initializeLogging();
        
        // Get the directory where this executable is located
        executable_dir = getExecutableDirectory(argv0);
        
        // Note: Debug output during constructor can interfere with MCP protocol initialization
        // Debug messages are only output during actual operations, not during startup
        // Get configuration from environment
        const char* port_env = std::getenv("X16_PORT");
        if (port_env) {
            emulator_port = std::atoi(port_env);
        }
        
        // Set emulator path - x16emu is in the same directory as this MCP executable
        emulator_path = executable_dir + "/" + std::string(EMULATOR_EXECUTABLE);
        
        // Initialize HTTP client
        http_client = new httplib::Client("127.0.0.1", emulator_port);
        http_client->set_connection_timeout(1, 0); // 1 second timeout
        http_client->set_read_timeout(5, 0);       // 5 second read timeout
        
        // Log startup (only after logging is initialized)
        logMessage(INFO, "MCP Server initialized", {
            {"emulator_port", emulator_port},
            {"emulator_path", emulator_path},
            {"executable_dir", executable_dir}
        });
    }
    
    ~X16EmulatorMCP() {
        if (http_client) {
            delete http_client;
        }
    }
    
    // Check if emulator is running by testing HTTP connection
    bool isEmulatorRunning() {
        if (!http_client) return false;
        
        auto res = http_client->Get("/status");
        emulator_running = (res && res->status == 200);
        return emulator_running;
    }
    
    // Start the emulator with specified parameters
    json startEmulator(const json& params) {
        if (isEmulatorRunning()) {
            return {
                {"success", false},
                {"error", "Emulator is already running"}
            };
        }
        
        std::string command = emulator_path;
        command += " -mcp " + std::to_string(emulator_port);
        
        // Add logging arguments - use logs/x16emu_log.txt as default
        std::string log_path = executable_dir + "/logs/x16emu_log.txt";
        command += " -log-file \"" + log_path + "\"";
        command += " -log-level INFO";
        
        // Add program file if specified
        if (params.contains("program") && !params["program"].empty()) {
            std::string program_path = params["program"].get<std::string>();
            command += " -prg \"" + program_path + "\"";
        }
        
        // Add run flag if specified
        if (params.contains("auto_run") && params["auto_run"].get<bool>()) {
            command += " -run";
        }
        
        // Add scale if specified
        if (params.contains("scale")) {
            int scale = params["scale"].get<int>();
            if (scale >= 1 && scale <= 4) {
                command += " -scale " + std::to_string(scale);
            }
        }
        
        // Add additional arguments if specified
        if (params.contains("args") && !params["args"].empty()) {
            command += " " + params["args"].get<std::string>();
        }
        
        if (debug_mode) {
            std::cerr << "MCP: Starting emulator with command: " << command << std::endl;
        }
        
        // Run in background with output redirected to log file
        // Use logs/x16emu_log.txt as specified in the task - relative paths are resolved against executable directory
        std::string output_log_path = executable_dir + "/logs/x16emu_log.txt";
        
        // Create logs directory if it doesn't exist
        try {
            std::filesystem::create_directories(executable_dir + "/logs");
        } catch (const std::exception& e) {
            if (debug_mode) {
                std::cerr << "MCP: Warning - could not create logs directory: " << e.what() << std::endl;
            }
        }
        
        // Launch emulator with proper working directory set to executable directory
        bool launch_success = false;
        
#ifdef _WIN32
        // Windows implementation using CreateProcess
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        // Redirect stdout and stderr to log file
        HANDLE hLogFile = CreateFileA(
            output_log_path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hLogFile != INVALID_HANDLE_VALUE) {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdOutput = hLogFile;
            si.hStdError = hLogFile;
        }
        
        // Create the process with working directory set to executable directory
        std::string full_command = command;
        launch_success = CreateProcessA(
            NULL,                           // No module name (use command line)
            const_cast<char*>(full_command.c_str()), // Command line
            NULL,                           // Process handle not inheritable
            NULL,                           // Thread handle not inheritable
            TRUE,                           // Set handle inheritance to TRUE (for log file)
            0,                              // No creation flags
            NULL,                           // Use parent's environment block
            executable_dir.c_str(),         // Set working directory to executable directory
            &si,                            // Pointer to STARTUPINFO structure
            &pi                             // Pointer to PROCESS_INFORMATION structure
        );
        
        if (launch_success) {
            // Close process and thread handles (we don't need to wait)
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        
        if (hLogFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hLogFile);
        }
        
#else
        // Unix implementation using fork/exec
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            
            // Change working directory to executable directory
            if (chdir(executable_dir.c_str()) != 0) {
                if (debug_mode) {
                    std::cerr << "MCP: Warning - could not change working directory to: " << executable_dir << std::endl;
                }
            }
            
            // Redirect stdout and stderr to log file
            freopen(output_log_path.c_str(), "w", stdout);
            freopen(output_log_path.c_str(), "a", stderr);
            
            // Parse command into arguments for execvp
            std::vector<std::string> args;
            std::istringstream iss(command);
            std::string arg;
            
            // Simple argument parsing (handles quoted arguments)
            bool in_quotes = false;
            std::string current_arg;
            
            for (char c : command) {
                if (c == '"' && !in_quotes) {
                    in_quotes = true;
                } else if (c == '"' && in_quotes) {
                    in_quotes = false;
                } else if (c == ' ' && !in_quotes) {
                    if (!current_arg.empty()) {
                        args.push_back(current_arg);
                        current_arg.clear();
                    }
                } else {
                    current_arg += c;
                }
            }
            if (!current_arg.empty()) {
                args.push_back(current_arg);
            }
            
            // Convert to char* array for execvp
            std::vector<char*> argv;
            for (auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);
            
            // Execute the emulator
            execvp(argv[0], argv.data());
            
            // If we get here, execvp failed
            std::cerr << "MCP: Failed to execute emulator: " << strerror(errno) << std::endl;
            exit(1);
            
        } else if (pid > 0) {
            // Parent process - child launched successfully
            launch_success = true;
        } else {
            // Fork failed
            if (debug_mode) {
                std::cerr << "MCP: Fork failed: " << strerror(errno) << std::endl;
            }
            launch_success = false;
        }
#endif
        
        if (!launch_success) {
            if (debug_mode) {
                std::cerr << "MCP: Failed to launch emulator process" << std::endl;
            }
        }
        
        // Wait a moment for emulator to start
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        
        // Check if it started successfully
        if (isEmulatorRunning()) {
            return {
                {"success", true},
                {"message", "Emulator started successfully"},
                {"command", command},
                {"log_file", log_path}
            };
        } else {
            return {
                {"success", false},
                {"error", "Failed to start emulator or emulator not responding"},
                {"command", command},
                {"log_file", log_path}
            };
        }
    }
    
    // Stop the emulator
    json stopEmulator() {
        return makeEmulatorRequest("/shutdown", "shutdown");
    }
    
    // Take system snapshot (returns system state data AND screenshot)
    json takeSnapshot() {
        logMessage(INFO, "takeSnapshot called");
        
        if (!isEmulatorRunning()) {
            logMessage(ERROR, "takeSnapshot failed: emulator not running");
            return {
                {"success", false},
                {"error", "Emulator is not running. Use start_emulator tool first."}
            };
        }
        
        auto res = http_client->Post("/snapshot", "{}", "application/json");
        logHTTPTransaction("POST", "/snapshot", res ? res->status : -1, res ? res->body : "");
        
        if (res && res->status == 200) {
            try {
                auto response_json = json::parse(res->body);
                logMessage(DEBUG, "Snapshot HTTP response parsed", response_json);
                
                // Check if the emulator returned an error status
                if (response_json.contains("status") && response_json["status"].get<std::string>() == "error") {
                    std::string error_msg = response_json.value("message", "Unknown emulator error");
                    logMessage(ERROR, "Emulator returned error status", {
                        {"error_message", error_msg},
                        {"full_response", response_json}
                    });
                    return {
                        {"success", false},
                        {"error", "Emulator snapshot failed: " + error_msg}
                    };
                }
                
                // Extract system state data AND screenshot path
                if (response_json.contains("data")) {
                    auto data = response_json["data"];
                    
                    // Also get the screenshot path if available
                    std::string uri;
                    if (response_json.contains("path")) {
                        std::string path = response_json["path"].get<std::string>();
                        uri = "x16://" + path;
                    }
                    
                    // Return the entire data object as the system state plus screenshot URI
                    json keys = json::array();
                    if (!data.empty()) {
                        for (auto& [key, value] : data.items()) {
                            keys.push_back(key);
                        }
                    }
                    logMessage(INFO, "System snapshot captured successfully", {
                        {"system_state_keys", keys},
                        {"screenshot_uri", uri}
                    });
                    
                    json result = {
                        {"success", true},
                        {"message", "System snapshot captured"},
                        {"system_state", data}
                    };
                    
                    // Include screenshot URI if available (for MCP protocol handler)
                    if (!uri.empty()) {
                        result["uri"] = uri;
                    }
                    
                    return result;
                } else {
                    logMessage(ERROR, "Snapshot response missing data field", response_json);
                    return {
                        {"success", false},
                        {"error", "Emulator response missing system data"}
                    };
                }
            } catch (const std::exception& e) {
                logMessage(ERROR, "Failed to parse snapshot response", {{"exception", e.what()}});
                return {
                    {"success", false},
                    {"error", "Failed to parse snapshot response: " + std::string(e.what())}
                };
            }
        } else {
            std::string error_detail = res ? ("HTTP " + std::to_string(res->status)) : "Connection failed";
            logMessage(ERROR, "Snapshot HTTP request failed", {
                {"status_code", res ? res->status : -1},
                {"error_detail", error_detail}
            });
            return {
                {"success", false},
                {"error", "Failed to capture system snapshot: " + error_detail}
            };
        }
    }
    
    // Restart the emulator
    json restartEmulator() {
        return makeEmulatorRequest("/restart", "restart");
    }
    
    // Reset the emulator
    json resetEmulator() {
        return makeEmulatorRequest("/reset", "reset");
    }
    
    // Send NMI to emulator
    json sendNMI() {
        return makeEmulatorRequest("/nmi", "send NMI");
    }
    
    // Take screenshot
    json takeScreenshot() {
        logMessage(INFO, "takeScreenshot called");
        
        if (!isEmulatorRunning()) {
            logMessage(ERROR, "takeScreenshot failed: emulator not running");
            return {
                {"success", false},
                {"error", "Emulator is not running. Use start_emulator tool first."}
            };
        }
        
        auto res = http_client->Post("/screenshot", "{}", "application/json");
        logHTTPTransaction("POST", "/screenshot", res ? res->status : -1, res ? res->body : "");
        
        if (res && res->status == 200) {
            try {
                auto response_json = json::parse(res->body);
                logMessage(DEBUG, "Screenshot HTTP response parsed", response_json);
                
                // Check if the emulator returned an error status
                if (response_json.contains("status") && response_json["status"].get<std::string>() == "error") {
                    std::string error_msg = response_json.value("message", "Unknown emulator error");
                    logMessage(ERROR, "Emulator returned error status", {
                        {"error_message", error_msg},
                        {"full_response", response_json}
                    });
                    return {
                        {"success", false},
                        {"error", "Emulator screenshot failed: " + error_msg}
                    };
                }
                
                // Use the path returned by the emulator and prefix with x16://
                if (response_json.contains("path")) {
                    std::string path = response_json["path"].get<std::string>();
                    std::string uri = "x16://" + path;
                    
                    logMessage(INFO, "Screenshot captured successfully", {
                        {"path", path},
                        {"uri", uri}
                    });
                    
                    return {
                        {"success", true},
                        {"message", "Screenshot captured"},
                        {"uri", uri},
                        {"data", response_json.value("data", nullptr)}
                    };
                } else {
                    logMessage(ERROR, "Screenshot response missing path field", response_json);
                    return {
                        {"success", false},
                        {"error", "Emulator response missing path field"}
                    };
                }
            } catch (const std::exception& e) {
                logMessage(ERROR, "Failed to parse screenshot response", {{"exception", e.what()}});
                return {
                    {"success", false},
                    {"error", "Failed to parse screenshot response: " + std::string(e.what())}
                };
            }
        } else {
            std::string error_detail = res ? ("HTTP " + std::to_string(res->status)) : "Connection failed";
            logMessage(ERROR, "Screenshot HTTP request failed", {
                {"status_code", res ? res->status : -1},
                {"error_detail", error_detail}
            });
            return {
                {"success", false},
                {"error", "Failed to capture screenshot: " + error_detail}
            };
        }
    }
    
    // Get emulator status
    json getStatus() {
        bool running = isEmulatorRunning();
        json status = {
            {"emulator_running", running},
            {"emulator_port", emulator_port},
            {"emulator_path", emulator_path}
        };
        
        if (running) {
            auto res = http_client->Get("/status");
            if (res && res->status == 200) {
                try {
                    auto emulator_status = json::parse(res->body);
                    status["emulator_info"] = emulator_status;
                } catch (const std::exception& e) {
                    status["emulator_info"] = "Failed to parse emulator status";
                }
            }
        }
        
        return status;
    }
    
    // Send keyboard input to the emulator
    json sendKeyboard(const json& params) {
        logMessage(INFO, "sendKeyboard called", params);
        
        if (!isEmulatorRunning()) {
            logMessage(ERROR, "sendKeyboard failed: emulator not running");
            return {
                {"success", false},
                {"error", "Emulator is not running. Use start_emulator tool first."}
            };
        }
        
        // Forward the request to the emulator's /keyboard endpoint
        auto res = http_client->Post("/keyboard", params.dump(), "application/json");
        logHTTPTransaction("POST", "/keyboard", res ? res->status : -1, res ? res->body : "");
        
        if (res && res->status == 200) {
            try {
                auto response_json = json::parse(res->body);
                logMessage(DEBUG, "Keyboard HTTP response parsed", response_json);
                
                // Check if the emulator returned an error status
                if (response_json.contains("status") && response_json["status"].get<std::string>() == "error") {
                    std::string error_msg = response_json.value("message", "Unknown emulator error");
                    logMessage(ERROR, "Emulator returned error status", {
                        {"error_message", error_msg},
                        {"full_response", response_json}
                    });
                    return {
                        {"success", false},
                        {"error", "Emulator keyboard input failed: " + error_msg},
                        {"emulator_response", response_json}
                    };
                }
                
                // Success case - return the emulator's response
                logMessage(INFO, "Keyboard input sent successfully");
                return {
                    {"success", true},
                    {"message", "Keyboard input sent to emulator"},
                    {"emulator_response", response_json}
                };
                
            } catch (const std::exception& e) {
                logMessage(ERROR, "Failed to parse keyboard response", {{"exception", e.what()}});
                return {
                    {"success", false},
                    {"error", "Failed to parse keyboard response: " + std::string(e.what())}
                };
            }
        } else {
            std::string error_detail = res ? ("HTTP " + std::to_string(res->status)) : "Connection failed";
            logMessage(ERROR, "Keyboard HTTP request failed", {
                {"status_code", res ? res->status : -1},
                {"error_detail", error_detail}
            });
            return {
                {"success", false},
                {"error", "Failed to send keyboard input: " + error_detail}
            };
        }
    }
    
    // Send joystick input to the emulator
    json sendJoystick(const json& params) {
        logMessage(INFO, "sendJoystick called", params);
        
        if (!isEmulatorRunning()) {
            logMessage(ERROR, "sendJoystick failed: emulator not running");
            return {
                {"success", false},
                {"error", "Emulator is not running. Use start_emulator tool first."}
            };
        }
        
        // Forward the request to the emulator's /joystick endpoint
        auto res = http_client->Post("/joystick", params.dump(), "application/json");
        logHTTPTransaction("POST", "/joystick", res ? res->status : -1, res ? res->body : "");
        
        if (res && res->status == 200) {
            try {
                auto response_json = json::parse(res->body);
                logMessage(DEBUG, "Joystick HTTP response parsed", response_json);
                
                // Check if the emulator returned an error status
                if (response_json.contains("status") && response_json["status"].get<std::string>() == "error") {
                    std::string error_msg = response_json.value("message", "Unknown emulator error");
                    logMessage(ERROR, "Emulator returned error status", {
                        {"error_message", error_msg},
                        {"full_response", response_json}
                    });
                    return {
                        {"success", false},
                        {"error", "Emulator joystick input failed: " + error_msg},
                        {"emulator_response", response_json}
                    };
                }
                
                // Success case - return the emulator's response
                logMessage(INFO, "Joystick input sent successfully");
                return {
                    {"success", true},
                    {"message", "Joystick input sent to emulator"},
                    {"emulator_response", response_json}
                };
                
            } catch (const std::exception& e) {
                logMessage(ERROR, "Failed to parse joystick response", {{"exception", e.what()}});
                return {
                    {"success", false},
                    {"error", "Failed to parse joystick response: " + std::string(e.what())}
                };
            }
        } else {
            std::string error_detail = res ? ("HTTP " + std::to_string(res->status)) : "Connection failed";
            logMessage(ERROR, "Joystick HTTP request failed", {
                {"status_code", res ? res->status : -1},
                {"error_detail", error_detail}
            });
            return {
                {"success", false},
                {"error", "Failed to send joystick input: " + error_detail}
            };
        }
    }
    
    // List available resources (screenshots)
    // CRITICAL: This method is called during MCP protocol initialization.
    // ANY debug output here without debug_mode check will break MCP client connection!
    json listResources() {
        json resources = json::array();
        
        try {
            // Try multiple possible paths for screenshots
            std::vector<std::string> possible_paths = {
                executable_dir + "/resources/screenshots",
                "./resources/screenshots",
                "resources/screenshots"
            };
            
            std::string screenshots_path;
            bool found_path = false;
            
            for (const auto& path : possible_paths) {
                if (std::filesystem::exists(path)) {
                    screenshots_path = path;
                    found_path = true;
                    // Note: Debug output removed to prevent MCP protocol interference
                    break;
                }
            }
            
            if (found_path) {
                // Scan date directories for screenshots
                for (const auto& entry : std::filesystem::directory_iterator(screenshots_path)) {
                    if (entry.is_directory()) {
                        std::string dir_name = entry.path().filename().string();
                        
                        // Check if it's a date directory (YYYY-MM-DD format)
                        std::regex date_regex(R"(\d{4}-\d{2}-\d{2})");
                        if (std::regex_match(dir_name, date_regex)) {
                            // Scan for PNG files in this date directory
                            for (const auto& file_entry : std::filesystem::directory_iterator(entry.path())) {
                                if (file_entry.is_regular_file() && 
                                    file_entry.path().extension() == ".png") {
                                    
                                    std::string filename = file_entry.path().filename().string();
                                    std::string uri = "x16://screenshot/" + dir_name + "/" + filename;
                                    
                                    resources.push_back({
                                        {"uri", uri},
                                        {"mimeType", "image/png"},
                                        {"name", "Screenshot " + filename},
                                        {"description", "Screenshot from " + dir_name}
                                    });
                                }
                            }
                        }
                    }
                }
            } else {
                if (debug_mode) {
                    std::cerr << "MCP: No screenshots directory found in any of the expected locations" << std::endl;
                    std::cerr << "MCP: Executable dir: " << executable_dir << std::endl;
                    std::cerr << "MCP: Current working dir: " << std::filesystem::current_path() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            if (debug_mode) {
                std::cerr << "MCP: Error listing resources: " << e.what() << std::endl;
            }
        }
        
        return resources;
    }
    
    // Read a specific resource
    json readResource(const std::string& uri) {
        try {
            // Parse x16://screenshot/ URIs
            if (uri.substr(0, 17) == "x16://screenshot/") {
                std::string path_part = uri.substr(17);
                
                // Use the correct single path for screenshots
                std::string file_path = executable_dir + "/resources/screenshots/" + path_part;
                
                if (debug_mode) {
                    std::cerr << "MCP: Attempting to read resource from: " << file_path << std::endl;
                }
                
                if (std::filesystem::exists(file_path)) {
                    if (debug_mode) {
                        std::cerr << "MCP: File exists, reading..." << std::endl;
                    }
                    
                    // Read the PNG file as binary data
                    std::ifstream file(file_path, std::ios::binary);
                    if (file) {
                        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
                        
                        if (debug_mode) {
                            std::cerr << "MCP: Read " << buffer.size() << " bytes from file" << std::endl;
                        }
                        
                        // Convert to base64 for JSON transport
                        std::string base64_data = base64_encode(
                            reinterpret_cast<const unsigned char*>(buffer.data()), 
                            buffer.size()
                        );
                        
                        if (debug_mode) {
                            std::cerr << "MCP: Base64 encoded to " << base64_data.length() << " characters" << std::endl;
                        }
                        
                        return {
                            {"contents", {
                                {
                                    {"uri", uri},
                                    {"mimeType", "image/png"},
                                    {"blob", base64_data}
                                }
                            }}
                        };
                    } else {
                        if (debug_mode) {
                            std::cerr << "MCP: Failed to open file for reading" << std::endl;
                        }
                    }
                } else {
                    if (debug_mode) {
                        std::cerr << "MCP: File does not exist: " << file_path << std::endl;
                    }
                }
            }
            
            return {
                {"error", {
                    {"code", -32602},
                    {"message", "Resource not found: " + uri}
                }}
            };
            
        } catch (const std::exception& e) {
            if (debug_mode) {
                std::cerr << "MCP: Exception reading resource: " << e.what() << std::endl;
            }
            return {
                {"error", {
                    {"code", -32603},
                    {"message", "Error reading resource: " + std::string(e.what())}
                }}
            };
        }
    }
    
    // Base64 encoding helper
    std::string base64_encode(const unsigned char* data, size_t len) {
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int val = 0, valb = -6;
        for (size_t i = 0; i < len; ++i) {
            val = (val << 8) + data[i];
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (result.size() % 4) result.push_back('=');
        return result;
    }
    
    // Initialize logging system
    void initializeLogging() {
        // Get log level from environment
        const char* log_level_env = std::getenv("X16_LOG_LEVEL");
        if (log_level_env) {
            std::string level_str(log_level_env);
            if (level_str == "ERROR") {
                log_level = ERROR;
            } else if (level_str == "INFO") {
                log_level = INFO;
            } else if (level_str == "DEBUG") {
                log_level = DEBUG;
            }
        }
        
        // Enable logging if log level is set or debug mode is enabled
        logging_enabled = (log_level_env != nullptr) || debug_mode;
        
        if (logging_enabled) {
            // Create logs directory if it doesn't exist
            std::string logs_dir = executable_dir + "/logs";
            try {
                std::filesystem::create_directories(logs_dir);
                
                // Create log file with timestamp
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                auto tm = *std::localtime(&time_t);
                
                std::ostringstream filename;
                filename << logs_dir << "/mcp_server_" 
                        << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".log";
                log_file_path = filename.str();
                
                // Write initial log entry
                std::ofstream log_file(log_file_path, std::ios::app);
                if (log_file.is_open()) {
                    log_file << "=== MCP Server Log Started ===" << std::endl;
                    log_file << "Timestamp: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << std::endl;
                    log_file << "Log Level: " << (log_level == ERROR ? "ERROR" : log_level == INFO ? "INFO" : "DEBUG") << std::endl;
                    log_file << "Process ID: " << getpid() << std::endl;
                    log_file << "==============================" << std::endl;
                    log_file.close();
                }
            } catch (const std::exception& e) {
                // If logging setup fails, disable it to avoid breaking MCP protocol
                logging_enabled = false;
            }
        }
    }
    
    // Get current timestamp string
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream timestamp;
        timestamp << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return timestamp.str();
    }
    
    // Log a message with optional JSON data
    void logMessage(LogLevel level, const std::string& message, const json& data = json::object()) {
        if (!logging_enabled || level > log_level) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(log_mutex);
        
        try {
            std::ofstream log_file(log_file_path, std::ios::app);
            if (log_file.is_open()) {
                std::string level_str = (level == ERROR ? "ERROR" : level == INFO ? "INFO" : "DEBUG");
                log_file << "[" << getCurrentTimestamp() << "] [" << level_str << "] " << message;
                
                if (!data.empty()) {
                    log_file << " | Data: " << data.dump();
                }
                
                log_file << std::endl;
                log_file.close();
            }
        } catch (const std::exception& e) {
            // Silently fail to avoid breaking MCP protocol
        }
    }
    
    // Log MCP request/response
    void logMCPTransaction(const std::string& direction, const std::string& method, const json& data) {
        if (!logging_enabled) return;
        
        json log_data = {
            {"direction", direction},
            {"method", method},
            {"data", data}
        };
        
        logMessage(DEBUG, "MCP Transaction", log_data);
    }
    
    // Log HTTP request/response
    void logHTTPTransaction(const std::string& method, const std::string& endpoint, int status_code, const std::string& response_body = "") {
        if (!logging_enabled) return;
        
        json log_data = {
            {"http_method", method},
            {"endpoint", endpoint},
            {"status_code", status_code}
        };
        
        if (!response_body.empty() && log_level >= DEBUG) {
            log_data["response_body"] = response_body;
        }
        
        logMessage(DEBUG, "HTTP Transaction", log_data);
    }
    
    // Standardized HTTP request handler for emulator endpoints
    json makeEmulatorRequest(const std::string& endpoint, const std::string& operation_name) {
        logMessage(INFO, operation_name + " called");
        
        if (!isEmulatorRunning()) {
            logMessage(ERROR, operation_name + " failed: emulator not running");
            return {
                {"success", false},
                {"error", "Emulator is not running. Use start_emulator tool first."},
                {"http_details", {
                    {"endpoint", endpoint},
                    {"status", "emulator_not_running"}
                }}
            };
        }
        
        auto res = http_client->Post(endpoint, "{}", "application/json");
        logHTTPTransaction("POST", endpoint, res ? res->status : -1, res ? res->body : "");
        
        // Build detailed error information
        json http_details = {
            {"endpoint", endpoint},
            {"method", "POST"},
            {"request_body", "{}"},
            {"request_headers", {{"Content-Type", "application/json"}}}
        };
        
        if (res) {
            http_details["status_code"] = res->status;
            http_details["response_body"] = res->body;
            
            // Add response headers if available
            if (!res->headers.empty()) {
                json response_headers = json::object();
                for (const auto& header : res->headers) {
                    response_headers[header.first] = header.second;
                }
                http_details["response_headers"] = response_headers;
            }
            
            if (res->status == 200) {
                try {
                    // Try to parse response as JSON to check for emulator-level errors
                    auto response_json = json::parse(res->body);
                    logMessage(DEBUG, operation_name + " HTTP response parsed", response_json);
                    
                    // Check if the emulator returned an error status
                    if (response_json.contains("status") && response_json["status"].get<std::string>() == "error") {
                        std::string error_msg = response_json.value("message", "Unknown emulator error");
                        logMessage(ERROR, "Emulator returned error status", {
                            {"error_message", error_msg},
                            {"full_response", response_json}
                        });
                        return {
                            {"success", false},
                            {"error", "Emulator " + operation_name + " failed: " + error_msg},
                            {"http_details", http_details},
                            {"emulator_response", response_json}
                        };
                    }
                    
                    // Success case
                    logMessage(INFO, operation_name + " completed successfully");
                    return {
                        {"success", true},
                        {"message", operation_name + " completed successfully"}
                    };
                    
                } catch (const std::exception& e) {
                    // Response is not JSON or malformed - but HTTP 200 means it probably worked
                    logMessage(INFO, operation_name + " completed (non-JSON response)", {
                        {"parse_error", e.what()},
                        {"response_body", res->body}
                    });
                    return {
                        {"success", true},
                        {"message", operation_name + " completed successfully"},
                        {"http_details", http_details},
                        {"note", "Response was not JSON: " + std::string(e.what())}
                    };
                }
            } else {
                // HTTP error status
                std::string error_detail = "HTTP " + std::to_string(res->status);
                if (!res->body.empty()) {
                    error_detail += " - " + res->body;
                }
                
                logMessage(ERROR, operation_name + " HTTP request failed", {
                    {"status_code", res->status},
                    {"response_body", res->body}
                });
                
                return {
                    {"success", false},
                    {"error", "Failed to " + operation_name + ": " + error_detail},
                    {"http_details", http_details}
                };
            }
        } else {
            // Connection failed
            http_details["status"] = "connection_failed";
            logMessage(ERROR, operation_name + " connection failed");
            
            return {
                {"success", false},
                {"error", "Failed to " + operation_name + ": Connection to emulator failed"},
                {"http_details", http_details}
            };
        }
    }
    
    // Log field name usage for debugging MCP compatibility
    void logFieldUsage(const std::string& context, const std::string& field_name, const std::string& field_type) {
        if (!logging_enabled) return;
        
        json log_data = {
            {"context", context},
            {"field_name", field_name},
            {"field_type", field_type}
        };
        
        logMessage(INFO, "Field Usage", log_data);
    }
    
    // Handle MCP tool calls
    json handleToolCall(const std::string& tool_name, const json& arguments) {
        logMessage(INFO, "Tool call received", {
            {"tool_name", tool_name},
            {"arguments", arguments}
        });
        
        if (debug_mode) {
            std::cerr << "MCP: Tool call: " << tool_name << " with args: " << arguments.dump() << std::endl;
        }
        
        json result;
        if (tool_name == "start_emulator") {
            result = startEmulator(arguments);
        } else if (tool_name == "stop_emulator") {
            result = stopEmulator();
        } else if (tool_name == "reset_emulator") {
            result = resetEmulator();
        } else if (tool_name == "send_nmi") {
            result = sendNMI();
        } else if (tool_name == "take_screenshot") {
            result = takeScreenshot();
        } else if (tool_name == "take_snapshot") {
            result = takeSnapshot();
        } else if (tool_name == "restart_emulator") {
            result = restartEmulator();
        } else if (tool_name == "get_status") {
            result = getStatus();
        } else if (tool_name == "send_keyboard") {
            result = sendKeyboard(arguments);
        } else if (tool_name == "send_joystick") {
            result = sendJoystick(arguments);
        } else {
            result = {
                {"success", false},
                {"error", "Unknown tool: " + tool_name}
            };
        }
        
        logMessage(INFO, "Tool call completed", {
            {"tool_name", tool_name},
            {"success", result.value("success", false)},
            {"result_keys", json::array()}
        });
        
        // Log result keys for debugging
        json result_keys = json::array();
        for (auto& [key, value] : result.items()) {
            result_keys.push_back(key);
        }
        logMessage(DEBUG, "Tool result structure", {
            {"tool_name", tool_name},
            {"result_keys", result_keys}
        });
        
        return result;
    }
    
    // MCP Protocol Implementation
    void run() {
        std::string line;
        
        if (debug_mode) {
            std::cerr << "MCP: X16 Emulator MCP Server starting..." << std::endl;
        }
        
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            
            try {
                auto request = json::parse(line);
                auto response = handleMCPRequest(request);
                
                std::cout << response.dump() << std::endl;
                std::cout.flush();
                
            } catch (const std::exception& e) {
                // Send error response
                json error_response = {
                    {"jsonrpc", "2.0"},
                    {"id", nullptr},
                    {"error", {
                        {"code", -32700},
                        {"message", "Parse error: " + std::string(e.what())}
                    }}
                };
                std::cout << error_response.dump() << std::endl;
                std::cout.flush();
            }
        }
    }
    
private:
    json handleMCPRequest(const json& request) {
        // Log incoming request
        logMCPTransaction("INCOMING", request.value("method", "unknown"), request);
        
        // Basic JSON-RPC 2.0 validation
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            json error_response = {
                {"jsonrpc", "2.0"},
                {"id", request.value("id", nullptr)},
                {"error", {
                    {"code", -32600},
                    {"message", "Invalid Request"}
                }}
            };
            logMCPTransaction("OUTGOING", "error", error_response);
            return error_response;
        }
        
        std::string method = request.value("method", "");
        json id = nullptr;
        if (request.contains("id")) {
            id = request["id"];
        }
        
        logMessage(INFO, "Processing MCP request", {
            {"method", method},
            {"id", id}
        });
        
        if (method == "initialize") {
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"tools", json::object()},
                        {"resources", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", "x16-emulator-mcp"},
                        {"version", "1.0.0"}
                    }}
                }}
            };
        } else if (method == "tools/list") {
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"tools", {
                        {
                            {"name", "start_emulator"},
                            {"description", "Start the X16 emulator with optional program"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"program", {"type", "string", "description", "Path to program file (.prg)"}},
                                    {"auto_run", {"type", "boolean", "description", "Automatically run the program"}},
                                    {"scale", {"type", "integer", "description", "Display scale (1-4)", "minimum", 1, "maximum", 4}},
                                    {"args", {"type", "string", "description", "Additional command line arguments"}}
                                }}
                            }}
                        },
                        {
                            {"name", "stop_emulator"},
                            {"description", "Stop the X16 emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "reset_emulator"},
                            {"description", "Reset the X16 emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "send_nmi"},
                            {"description", "Send NMI (Non-Maskable Interrupt) to the emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "take_screenshot"},
                            {"description", "Capture a screenshot of the emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "take_snapshot"},
                            {"description", "Capture system state snapshot (CPU, memory, VERA registers) with screenshot"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "restart_emulator"},
                            {"description", "Restart the X16 emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "get_status"},
                            {"description", "Get the current status of the emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", json::object()}
                            }}
                        },
                        {
                            {"name", "send_keyboard"},
                            {"description", "Send keyboard input to the emulator. ASCII mode (default) converts lowercase to uppercase. Use backticks for macros: `ENTER`, `F1`, `UP`, `DOWN`, `HOME`, `CLR`. Escape sequences: \\t (TAB), \\n (ENTER). Pauses: `_500` (500ms), `_1.5` (1.5 seconds). PETSCII mode examples: colors `RED`, `BLUE`, symbols `HEART`, `SPADE`. Key names follow Commodore documentation (e.g., `CRSR UP`, `CRSR DOWN`, `INST DEL`). Example: \"hello`ENTER`\""},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"text", {"type", "string", "description", "Text to type. ASCII: lowercase becomes uppercase. Use backticks for special keys and pauses. Escape sequences: \\t (TAB), \\n (ENTER). Pauses: `_500` (milliseconds), `_1.5` (seconds). PETSCII examples: `RED`, `BLUE`, `HEART`, `SPADE`. Key names follow Commodore conventions."}},
                                    {"key", {"type", "string", "description", "Single key to send (e.g., 'ENTER', 'ESCAPE', 'F1')"}},
                                    {"pressed", {"type", "boolean", "description", "Whether key is pressed (true) or released (false), default true"}},
                                    {"mode", {"type", "string", "description", "Input mode: 'ascii' (default), 'petscii', 'screen'"}}
                                }}
                            }}
                        },
                        {
                            {"name", "send_joystick"},
                            {"description", "Send joystick input commands to the emulator"},
                            {"inputSchema", {
                                {"type", "object"},
                                {"properties", {
                                    {"commands", {"type", "string", "description", "Whitespace-delimited joystick commands (e.g., 'up fire left pause:500 down')"}},
                                    {"joystick", {"type", "integer", "description", "Joystick number (1 or 2), default 1", "minimum", 1, "maximum", 2}}
                                }}
                            }}
                        }
                    }}
                }}
            };
        } else if (method == "tools/call") {
            auto params = request.value("params", json::object());
            std::string tool_name = params.value("name", "");
            auto arguments = params.value("arguments", json::object());
            
            auto result = handleToolCall(tool_name, arguments);
            
            // Special handling for screenshot tool - return image content only
            if (tool_name == "take_screenshot" && 
                result.contains("success") && result["success"].get<bool>()) {
                
                if (result.contains("uri")) {
                    std::string uri = result["uri"].get<std::string>();
                    
                    // Read the screenshot file and return as ImageContent
                    auto resource_result = readResource(uri);
                    if (resource_result.contains("contents") && resource_result["contents"].is_array() && 
                        !resource_result["contents"].empty()) {
                        
                        auto content = resource_result["contents"][0];
                        if (content.contains("blob")) {
                            logFieldUsage("tool_response_image", "blob", "resource_field");
                            logFieldUsage("tool_response_image", "data", "mcp_tool_response_field");
                            
                            return {
                                {"jsonrpc", "2.0"},
                                {"id", id},
                                {"result", {
                                    {"content", {
                                        {
                                            {"type", "image"},
                                            {"data", content["blob"]},
                                            {"mimeType", "image/png"},
                                            {"annotations", {
                                                {"audience", {"assistant"}},
                                                {"priority", 0.9}
                                            }}
                                        }
                                    }}
                                }}
                            };
                        } else {
                            logMessage(ERROR, "Resource content missing blob field", {
                                {"uri", uri},
                                {"content_keys", json::array()}
                            });
                            
                            // Log available keys for debugging
                            json content_keys = json::array();
                            for (auto& [key, value] : content.items()) {
                                content_keys.push_back(key);
                            }
                            logMessage(DEBUG, "Available content keys", {
                                {"content_keys", content_keys}
                            });
                        }
                    }
                }
            }
            
            // Special handling for snapshot tool - return both system state AND image
            if (tool_name == "take_snapshot" && 
                result.contains("success") && result["success"].get<bool>()) {
                
                json content_array = json::array();
                
                // Add system state data as text content
                if (result.contains("system_state")) {
                    std::string system_state_text = result["system_state"].dump(2);
                    content_array.push_back({
                        {"type", "text"},
                        {"text", system_state_text},
                        {"annotations", {
                            {"audience", {"assistant"}},
                            {"priority", 0.8}
                        }}
                    });
                }
                
                // Add screenshot as image content
                if (result.contains("uri")) {
                    std::string uri = result["uri"].get<std::string>();
                    
                    // Read the screenshot file and add as ImageContent
                    auto resource_result = readResource(uri);
                    if (resource_result.contains("contents") && resource_result["contents"].is_array() && 
                        !resource_result["contents"].empty()) {
                        
                        auto content = resource_result["contents"][0];
                        if (content.contains("blob")) {
                            content_array.push_back({
                                {"type", "image"},
                                {"data", content["blob"]},
                                {"mimeType", "image/png"},
                                {"annotations", {
                                    {"audience", {"assistant"}},
                                    {"priority", 0.9}
                                }}
                            });
                        }
                    }
                }
                
                // Return both system state and image if we have content
                if (!content_array.empty()) {
                    return {
                        {"jsonrpc", "2.0"},
                        {"id", id},
                        {"result", {
                            {"content", content_array}
                        }}
                    };
                }
            }
            
            // Default: return as text content
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"content", {
                        {
                            {"type", "text"},
                            {"text", result.dump(2)}
                        }
                    }}
                }}
            };
        } else if (method == "resources/list") {
            auto resources = listResources();
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"resources", resources}
                }}
            };
        } else if (method == "resources/read") {
            auto params = request.value("params", json::object());
            std::string uri = params.value("uri", "");
            
            if (uri.empty()) {
                return {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {
                        {"code", -32602},
                        {"message", "Missing required parameter: uri"}
                    }}
                };
            }
            
            auto result = readResource(uri);
            
            if (result.contains("error")) {
                return {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", result["error"]}
                };
            } else {
                return {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", result}
                };
            }
        } else {
            return {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"error", {
                    {"code", -32601},
                    {"message", "Method not found: " + method}
                }}
            };
        }
    }
};

int main(int argc, char* argv[]) {
    try {
        X16EmulatorMCP server(argv[0]);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "MCP Server Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
