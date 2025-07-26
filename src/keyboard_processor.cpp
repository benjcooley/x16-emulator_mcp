#include "keyboard_processor.h"
#include <unordered_map>
#include <regex>
#include <cctype>
#include <sstream>

// X16 keyboard scan codes (from keyboard.c keynum_from_SDL_Scancode)
static const std::unordered_map<std::string, uint8_t> x16_keycodes = {
    // Function keys
    {"F1", 112}, {"F2", 113}, {"F3", 114}, {"F4", 115},
    {"F5", 116}, {"F6", 117}, {"F7", 118}, {"F8", 119},
    {"F9", 120}, {"F10", 121}, {"F11", 122}, {"F12", 123},
    
    // Cursor keys
    {"UP", 83}, {"DOWN", 84}, {"LEFT", 79}, {"RIGHT", 89},
    
    // Navigation
    {"HOME", 80}, {"END", 81}, {"PAGEUP", 85}, {"PAGEDOWN", 86},
    {"INSERT", 75}, {"DELETE", 76},
    
    // Control keys
    {"ENTER", 43}, {"RETURN", 43}, {"TAB", 16}, {"BACKSPACE", 15},
    {"ESCAPE", 110}, {"SPACE", 61},
    
    // Modifier keys
    {"LSHIFT", 44}, {"RSHIFT", 57}, {"LCTRL", 58}, {"RCTRL", 64},
    {"LALT", 60}, {"RALT", 62}, {"CAPSLOCK", 30},
};

// Joystick button mappings (based on SDL controller buttons and X16 joystick.c)
// X16 supports up to 4 joysticks (NUM_JOYSTICKS = 4)
static const std::unordered_map<std::string, uint8_t> joystick_buttons = {
    // Joystick 1 (Player 1) - Default joystick
    {"JOY1_A", 0}, {"JOY1_B", 8}, {"JOY1_X", 1}, {"JOY1_Y", 9},
    {"JOY1_BACK", 2}, {"JOY1_START", 3}, {"JOY1_L", 10}, {"JOY1_R", 11},
    {"JOY1_UP", 4}, {"JOY1_DOWN", 5}, {"JOY1_LEFT", 6}, {"JOY1_RIGHT", 7},
    
    // Joystick 2 (Player 2)
    {"JOY2_A", 0}, {"JOY2_B", 8}, {"JOY2_X", 1}, {"JOY2_Y", 9},
    {"JOY2_BACK", 2}, {"JOY2_START", 3}, {"JOY2_L", 10}, {"JOY2_R", 11},
    {"JOY2_UP", 4}, {"JOY2_DOWN", 5}, {"JOY2_LEFT", 6}, {"JOY2_RIGHT", 7},
    
    // Joystick 3 (Player 3)
    {"JOY3_A", 0}, {"JOY3_B", 8}, {"JOY3_X", 1}, {"JOY3_Y", 9},
    {"JOY3_BACK", 2}, {"JOY3_START", 3}, {"JOY3_L", 10}, {"JOY3_R", 11},
    {"JOY3_UP", 4}, {"JOY3_DOWN", 5}, {"JOY3_LEFT", 6}, {"JOY3_RIGHT", 7},
    
    // Joystick 4 (Player 4)
    {"JOY4_A", 0}, {"JOY4_B", 8}, {"JOY4_X", 1}, {"JOY4_Y", 9},
    {"JOY4_BACK", 2}, {"JOY4_START", 3}, {"JOY4_L", 10}, {"JOY4_R", 11},
    {"JOY4_UP", 4}, {"JOY4_DOWN", 5}, {"JOY4_LEFT", 6}, {"JOY4_RIGHT", 7},
    
    // Generic joystick names (default to joystick 1)
    {"JOY_A", 0}, {"JOY_B", 8}, {"JOY_X", 1}, {"JOY_Y", 9},
    {"JOY_BACK", 2}, {"JOY_START", 3}, {"JOY_L", 10}, {"JOY_R", 11},
    {"JOY_UP", 4}, {"JOY_DOWN", 5}, {"JOY_LEFT", 6}, {"JOY_RIGHT", 7},
    
    // Alternative names for common usage
    {"DPAD_UP", 4}, {"DPAD_DOWN", 5}, {"DPAD_LEFT", 6}, {"DPAD_RIGHT", 7},
    {"BUTTON_A", 0}, {"BUTTON_B", 8}, {"BUTTON_X", 1}, {"BUTTON_Y", 9},
    {"SELECT", 2}, {"START", 3}, {"L_SHOULDER", 10}, {"R_SHOULDER", 11},
};

// PETSCII character mappings (comprehensive set)
static const std::unordered_map<std::string, uint8_t> petscii_chars = {
    // Card suits & symbols
    {"HEART", 83}, {"DIAMOND", 90}, {"CLUB", 88}, {"SPADE", 85},
    {"BALL", 81}, {"CIRCLE", 79}, {"CROSS", 78}, {"STAR", 42},
    
    // Box drawing characters
    {"HLINE", 192}, {"VLINE", 221}, 
    {"ULCORNER", 176}, {"URCORNER", 174}, {"LLCORNER", 173}, {"LRCORNER", 189},
    {"CROSS4", 219}, {"TEE_UP", 177}, {"TEE_DOWN", 178}, 
    {"TEE_LEFT", 180}, {"TEE_RIGHT", 179},
    
    // Block characters
    {"BLOCK", 160}, {"LBLOCK", 161}, {"RBLOCK", 162}, 
    {"TBLOCK", 163}, {"BBLOCK", 164},
    
    // Special characters
    {"PI", 222}, {"POUND", 92}, {"UPARROW", 94}, {"LEFTARROW", 95},
    {"CHECKERBOARD", 102},
    
    // Colors (screen control codes)
    {"BLK", 144}, {"WHT", 5}, {"RED", 28}, {"CYN", 159}, 
    {"PUR", 156}, {"GRN", 30}, {"BLU", 31}, {"YEL", 158},
    {"ORG", 129}, {"BRN", 149}, {"LRED", 150}, {"GRY1", 151},
    {"GRY2", 152}, {"LGRN", 153}, {"LBLU", 154}, {"GRY3", 155},
    
    // Screen control
    {"CLR", 147}, {"HOME", 19}, {"RVS_ON", 18}, {"RVS_OFF", 146},
    {"INST", 148}, {"DEL", 20},
    
    // Additional PETSCII graphics
    {"CHECKMARK", 122}, {"CROSS_HATCH", 103}, {"DIAGONAL1", 104}, {"DIAGONAL2", 105},
    {"SOLID_SQUARE", 160}, {"LIGHT_SHADE", 161}, {"MEDIUM_SHADE", 162}, {"DARK_SHADE", 163},
};

// ASCII to PETSCII conversion table
static const std::unordered_map<char, uint8_t> ascii_to_petscii = {
    // Basic ASCII characters that map directly
    {' ', 32}, {'!', 33}, {'"', 34}, {'#', 35}, {'$', 36}, {'%', 37}, {'&', 38}, {'\'', 39},
    {'(', 40}, {')', 41}, {'*', 42}, {'+', 43}, {',', 44}, {'-', 45}, {'.', 46}, {'/', 47},
    {'0', 48}, {'1', 49}, {'2', 50}, {'3', 51}, {'4', 52}, {'5', 53}, {'6', 54}, {'7', 55},
    {'8', 56}, {'9', 57}, {':', 58}, {';', 59}, {'<', 60}, {'=', 61}, {'>', 62}, {'?', 63},
    {'@', 64}, 
    {'A', 65}, {'B', 66}, {'C', 67}, {'D', 68}, {'E', 69}, {'F', 70}, {'G', 71}, {'H', 72},
    {'I', 73}, {'J', 74}, {'K', 75}, {'L', 76}, {'M', 77}, {'N', 78}, {'O', 79}, {'P', 80},
    {'Q', 81}, {'R', 82}, {'S', 83}, {'T', 84}, {'U', 85}, {'V', 86}, {'W', 87}, {'X', 88},
    {'Y', 89}, {'Z', 90},
    {'[', 91}, {'\\', 92}, {']', 93}, {'^', 94}, {'_', 95},
    
    // Special mappings for control characters
    {'\n', 13}, {'\r', 13}, {'\t', 9}, {'\b', 8},
};

// Convert string to KeyboardMode
KeyboardMode string_to_keyboard_mode(const std::string& mode_str) {
    if (mode_str == "petscii") return KeyboardMode::PETSCII;
    if (mode_str == "raw") return KeyboardMode::RAW;
    return KeyboardMode::ASCII; // Default
}

// Convert KeyboardMode to string
std::string keyboard_mode_to_string(KeyboardMode mode) {
    switch (mode) {
        case KeyboardMode::PETSCII: return "petscii";
        case KeyboardMode::RAW: return "raw";
        case KeyboardMode::ASCII:
        default: return "ascii";
    }
}

// Process a single macro and return its keycode
static uint8_t process_macro(const std::string& macro, bool& success, std::string& error) {
    success = true;
    
    // Check for raw keycode
    if (macro.substr(0, 1) == "K" && macro.length() > 1) {
        try {
            int keycode = std::stoi(macro.substr(1));
            if (keycode < 0 || keycode > 255) {
                success = false;
                error = "Invalid keycode: " + macro;
                return 0;
            }
            return static_cast<uint8_t>(keycode);
        } catch (const std::exception&) {
            success = false;
            error = "Invalid keycode: " + macro;
            return 0;
        }
    }
    
    // Check for X16 key names
    if (x16_keycodes.find(macro) != x16_keycodes.end()) {
        return x16_keycodes.at(macro);
    }
    
    // Check for PETSCII character names
    if (petscii_chars.find(macro) != petscii_chars.end()) {
        return petscii_chars.at(macro);
    }
    
    // Check for joystick button names (for convenience in keyboard input)
    // Note: These map to keyboard equivalents for mixed input scenarios
    if (joystick_buttons.find(macro) != joystick_buttons.end()) {
        // Map joystick buttons to cursor keys and common keys for keyboard convenience
        uint8_t joy_button = joystick_buttons.at(macro);
        switch (joy_button) {
            case 4: return 83;  // JOY_UP -> UP arrow
            case 5: return 84;  // JOY_DOWN -> DOWN arrow  
            case 6: return 79;  // JOY_LEFT -> LEFT arrow
            case 7: return 89;  // JOY_RIGHT -> RIGHT arrow
            case 0: return 61;  // JOY_A -> SPACE
            case 8: return 43;  // JOY_B -> ENTER
            case 1: return 88;  // JOY_X -> 'X'
            case 9: return 89;  // JOY_Y -> 'Y'
            case 2: return 83;  // JOY_BACK -> 'S' (Select)
            case 3: return 83;  // JOY_START -> 'S' (Start)
            default: return 61; // Default to SPACE
        }
    }
    
    success = false;
    error = "Unknown macro: " + macro;
    return 0;
}

// Convert ASCII character to X16-compatible character (for ASCII mode)
static uint8_t ascii_to_x16(char c) {
    // Convert lowercase to uppercase
    if (c >= 'a' && c <= 'z') {
        return static_cast<uint8_t>(c - 'a' + 'A');
    }
    
    // Handle special characters
    switch (c) {
        case '\n': case '\r': return 13; // CR
        case '\t': return 9;             // TAB
        case '\b': return 8;             // Backspace
        default: return static_cast<uint8_t>(c);
    }
}

// Convert ASCII character to PETSCII
static uint8_t ascii_to_petscii_char(char c) {
    // Convert lowercase to uppercase first
    if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }
    
    // Look up in conversion table
    if (ascii_to_petscii.find(c) != ascii_to_petscii.end()) {
        return ascii_to_petscii.at(c);
    }
    
    // Default to direct mapping for characters not in table
    return static_cast<uint8_t>(c);
}

// Process text segments separated by pause commands
static std::vector<std::string> split_by_pauses(const std::string& input, std::vector<int>& pause_durations) {
    std::vector<std::string> segments;
    pause_durations.clear();
    
    // Support pause formats: `_500` (500ms), `_1.3` (1300ms)
    std::regex pause_regex(R"(`_(\d+(?:\.\d+)?)`)");
    std::sregex_token_iterator iter(input.begin(), input.end(), pause_regex, -1);
    std::sregex_token_iterator end;
    
    std::sregex_iterator pause_iter(input.begin(), input.end(), pause_regex);
    std::sregex_iterator pause_end;
    
    // Extract text segments
    for (; iter != end; ++iter) {
        segments.push_back(iter->str());
    }
    
    // Extract pause durations
    for (; pause_iter != pause_end; ++pause_iter) {
        double duration_seconds = std::stod(pause_iter->str(1));
        int duration_ms = static_cast<int>(duration_seconds * 1000);
        pause_durations.push_back(duration_ms);
    }
    
    return segments;
}

// Process macros in a text segment (excluding pause commands)
static std::string process_text_macros(const std::string& input, bool& success, std::string& error) {
    std::string result = input;
    size_t pos = 0;
    
    while ((pos = result.find('`', pos)) != std::string::npos) {
        size_t end_pos = result.find('`', pos + 1);
        if (end_pos == std::string::npos) {
            success = false;
            error = "Unclosed macro at position " + std::to_string(pos);
            return "";
        }
        
        std::string macro = result.substr(pos + 1, end_pos - pos - 1);
        
        // Skip pause commands (they're handled separately)
        if (macro.substr(0, 1) == "_" && macro.length() > 1) {
            pos = end_pos + 1;
            continue;
        }
        
        uint8_t keycode = process_macro(macro, success, error);
        if (!success) {
            return "";
        }
        
        // Replace macro with the keycode character
        result.replace(pos, end_pos - pos + 1, 1, static_cast<char>(keycode));
        pos++;
    }
    
    return result;
}

// Main processing function
ProcessedKeyboardData process_keyboard_input(const std::string& input, KeyboardMode mode) {
    ProcessedKeyboardData result;
    result.success = true;
    result.total_characters = 0;
    result.total_pause_time_ms = 0;
    
    if (mode == KeyboardMode::RAW) {
        // Raw mode: each byte is a direct X16 keycode
        for (char c : input) {
            result.keycodes.push_back(static_cast<uint8_t>(c));
        }
        result.total_characters = input.length();
        return result;
    }
    
    if (mode == KeyboardMode::ASCII) {
        // ASCII mode: process macros and pauses
        std::vector<int> pause_durations;
        std::vector<std::string> segments = split_by_pauses(input, pause_durations);
        
        for (size_t i = 0; i < segments.size(); i++) {
            // Process macros in this segment
            std::string processed_segment = process_text_macros(segments[i], result.success, result.error_message);
            if (!result.success) {
                return result;
            }
            
            // Convert characters to X16 format
            for (char c : processed_segment) {
                uint8_t keycode = ascii_to_x16(c);
                result.keycodes.push_back(keycode);
                result.total_characters++;
            }
            
            // Add pause if there's one after this segment
            if (i < pause_durations.size()) {
                result.pause_positions.push_back(result.keycodes.size());
                result.pause_durations.push_back(pause_durations[i]);
                result.total_pause_time_ms += pause_durations[i];
            }
        }
        
        return result;
    }
    
    if (mode == KeyboardMode::PETSCII) {
        // PETSCII mode: convert ASCII to PETSCII encoding
        for (char c : input) {
            uint8_t petscii_code = ascii_to_petscii_char(c);
            result.keycodes.push_back(petscii_code);
        }
        result.total_characters = input.length();
        return result;
    }
    
    result.success = false;
    result.error_message = "Unknown keyboard mode";
    return result;
}

// Joystick processing function - processes whitespace-delimited joystick commands
ProcessedKeyboardData process_joystick_input(const std::string& input, int joystick_num) {
    ProcessedKeyboardData result;
    result.success = true;
    result.total_characters = 0;
    result.total_pause_time_ms = 0;
    
    // Split input by whitespace
    std::istringstream iss(input);
    std::string token;
    
    while (iss >> token) {
        // Check for pause commands: _500, _1.3, PAUSE:500, PAUSE:1.3
        if (token.substr(0, 1) == "_" && token.length() > 1) {
            try {
                double duration_seconds = std::stod(token.substr(1));
                int duration_ms = static_cast<int>(duration_seconds * 1000);
                result.pause_positions.push_back(result.keycodes.size());
                result.pause_durations.push_back(duration_ms);
                result.total_pause_time_ms += duration_ms;
                continue;
            } catch (const std::exception&) {
                result.success = false;
                result.error_message = "Invalid pause duration: " + token;
                return result;
            }
        }
        
        if (token.substr(0, 6) == "PAUSE:" && token.length() > 6) {
            try {
                double duration_seconds = std::stod(token.substr(6));
                int duration_ms = static_cast<int>(duration_seconds * 1000);
                result.pause_positions.push_back(result.keycodes.size());
                result.pause_durations.push_back(duration_ms);
                result.total_pause_time_ms += duration_ms;
                continue;
            } catch (const std::exception&) {
                result.success = false;
                result.error_message = "Invalid pause duration: " + token;
                return result;
            }
        }
        
        // Process joystick button/direction commands
        if (joystick_buttons.find(token) != joystick_buttons.end()) {
            uint8_t button_code = joystick_buttons.at(token);
            // For joystick input, we encode as: joystick_num (4 bits) | button_code (4 bits)
            // This allows the emulator to distinguish joystick input from keyboard input
            uint8_t encoded = ((joystick_num & 0x0F) << 4) | (button_code & 0x0F);
            result.keycodes.push_back(encoded);
            result.total_characters++;
            continue;
        }
        
        // Check for generic joystick commands (default to specified joystick)
        std::string prefixed_token = "JOY" + std::to_string(joystick_num) + "_" + token;
        if (joystick_buttons.find(prefixed_token) != joystick_buttons.end()) {
            uint8_t button_code = joystick_buttons.at(prefixed_token);
            uint8_t encoded = ((joystick_num & 0x0F) << 4) | (button_code & 0x0F);
            result.keycodes.push_back(encoded);
            result.total_characters++;
            continue;
        }
        
        // Unknown command
        result.success = false;
        result.error_message = "Unknown joystick command: " + token;
        return result;
    }
    
    return result;
}
