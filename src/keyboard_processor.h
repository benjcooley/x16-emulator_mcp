#ifndef KEYBOARD_PROCESSOR_H
#define KEYBOARD_PROCESSOR_H

#include <string>
#include <vector>

// Keyboard processing modes
enum class KeyboardMode {
    ASCII,   // Default: natural text with macro support
    PETSCII, // ASCII to PETSCII character encoding
    RAW      // Direct X16 keyboard scan codes
};

// Structure to hold processed keyboard data
struct ProcessedKeyboardData {
    std::vector<uint8_t> keycodes;
    std::vector<int> pause_positions;  // Positions where pauses occur
    std::vector<int> pause_durations;  // Duration of each pause in ms
    int total_characters;
    int total_pause_time_ms;
    bool success;
    std::string error_message;
};

// Main processing function
ProcessedKeyboardData process_keyboard_input(const std::string& input, KeyboardMode mode);

// Joystick processing function - processes whitespace-delimited joystick commands
ProcessedKeyboardData process_joystick_input(const std::string& input, int joystick_num = 1);

// Helper functions
std::string keyboard_mode_to_string(KeyboardMode mode);
KeyboardMode string_to_keyboard_mode(const std::string& mode_str);

#endif // KEYBOARD_PROCESSOR_H
