#include "keyboard_processor.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <SDL.h>

// Include C headers with proper linkage
extern "C" {
#include "../logging.h"
}

// SDL types are now available from SDL.h

// External functions from keyboard.c
extern "C" {
    void handle_keyboard(bool down, SDL_Keycode sym, SDL_Scancode scancode);
    int keynum_from_SDL_Scancode(SDL_Scancode scancode);
    uint32_t SDL_GetTicks(void);
}

// Global queue manager for timer-based processing
static std::vector<InputEventQueue*> g_pending_queues;
static size_t g_current_event_index = 0;
static uint32_t g_last_event_time = 0;
static uint32_t g_elapsed_time = 0;
static bool g_processing_active = false;

// Timing constants (in milliseconds)
#define KEY_EVENT_MIN_DELAY_MS  5   // Standard delay for each key event (down/up)
#define KEY_EVENT_UP_DELAY_MS   10  // Maximum delay for key events (to prevent long waits)

// InputEventQueue implementation using std::vector
InputEventQueue::InputEventQueue(size_t initial_capacity) {
    events.reserve(initial_capacity);
}

InputEventQueue::~InputEventQueue() {
    // std::vector handles cleanup automatically
}

bool InputEventQueue::add_event(uint8_t type, uint16_t code, uint8_t is_down, uint32_t wait_ms) {
    events.emplace_back(InputEvent{type, is_down, code, wait_ms});
    return true;
}

size_t InputEventQueue::size() const {
    return events.size();
}

const InputEvent& InputEventQueue::operator[](size_t index) const {
    return events[index];
}

// Create new input queue
InputEventQueue* create_input_queue(size_t initial_capacity) {
    return new InputEventQueue(initial_capacity);
}

// ASCII character to SDL scancode mapping
struct CharacterMapping {
    char ascii_char;
    uint16_t scancode;
    bool needs_shift;
    bool needs_ctrl;  // For Commodore key combinations
};

// Character mapping table (using correct SDL scancode values)
static const CharacterMapping char_map[] = {
    // Letters (uppercase by default on X16)
    {'a', SDL_SCANCODE_A, false, false}, {'b', SDL_SCANCODE_B, false, false}, {'c', SDL_SCANCODE_C, false, false},
    {'d', SDL_SCANCODE_D, false, false}, {'e', SDL_SCANCODE_E, false, false}, {'f', SDL_SCANCODE_F, false, false},
    {'g', SDL_SCANCODE_G, false, false}, {'h', SDL_SCANCODE_H, false, false}, {'i', SDL_SCANCODE_I, false, false},
    {'j', SDL_SCANCODE_J, false, false}, {'k', SDL_SCANCODE_K, false, false}, {'l', SDL_SCANCODE_L, false, false},
    {'m', SDL_SCANCODE_M, false, false}, {'n', SDL_SCANCODE_N, false, false}, {'o', SDL_SCANCODE_O, false, false},
    {'p', SDL_SCANCODE_P, false, false}, {'q', SDL_SCANCODE_Q, false, false}, {'r', SDL_SCANCODE_R, false, false},
    {'s', SDL_SCANCODE_S, false, false}, {'t', SDL_SCANCODE_T, false, false}, {'u', SDL_SCANCODE_U, false, false},
    {'v', SDL_SCANCODE_V, false, false}, {'w', SDL_SCANCODE_W, false, false}, {'x', SDL_SCANCODE_X, false, false},
    {'y', SDL_SCANCODE_Y, false, false}, {'z', SDL_SCANCODE_Z, false, false},

    // NOTE: NEEDS SHIFT for upper case letters is calculated on the fly in the keyboard processor
    // so we can deal with lowercase charset mode and pescii charset mode correctly.

    {'A', SDL_SCANCODE_A, false, false}, {'B', SDL_SCANCODE_B, false, false}, {'C', SDL_SCANCODE_C, false, false},
    {'D', SDL_SCANCODE_D, false, false}, {'E', SDL_SCANCODE_E, false, false}, {'F', SDL_SCANCODE_F, false, false},
    {'G', SDL_SCANCODE_G, false, false}, {'H', SDL_SCANCODE_H, false, false}, {'I', SDL_SCANCODE_I, false, false},
    {'J', SDL_SCANCODE_J, false, false}, {'K', SDL_SCANCODE_K, false, false}, {'L', SDL_SCANCODE_L, false, false},
    {'M', SDL_SCANCODE_M, false, false}, {'N', SDL_SCANCODE_N, false, false}, {'O', SDL_SCANCODE_O, false, false},
    {'P', SDL_SCANCODE_P, false, false}, {'Q', SDL_SCANCODE_Q, false, false}, {'R', SDL_SCANCODE_R, false, false},
    {'S', SDL_SCANCODE_S, false, false}, {'T', SDL_SCANCODE_T, false, false}, {'U', SDL_SCANCODE_U, false, false},
    {'V', SDL_SCANCODE_V, false, false}, {'W', SDL_SCANCODE_W, false, false}, {'X', SDL_SCANCODE_X, false, false},
    {'Y', SDL_SCANCODE_Y, false, false}, {'Z', SDL_SCANCODE_Z, false, false},
    
    // Numbers
    {'0', SDL_SCANCODE_0, false, false}, {'1', SDL_SCANCODE_1, false, false}, {'2', SDL_SCANCODE_2, false, false},
    {'3', SDL_SCANCODE_3, false, false}, {'4', SDL_SCANCODE_4, false, false}, {'5', SDL_SCANCODE_5, false, false},
    {'6', SDL_SCANCODE_6, false, false}, {'7', SDL_SCANCODE_7, false, false}, {'8', SDL_SCANCODE_8, false, false},
    {'9', SDL_SCANCODE_9, false, false},
    
    // Special characters that need SHIFT
    {'!', SDL_SCANCODE_1, true, false},   // SHIFT + 1
    {'@', SDL_SCANCODE_2, true, false},   // SHIFT + 2
    {'#', SDL_SCANCODE_3, true, false},   // SHIFT + 3
    {'$', SDL_SCANCODE_4, true, false},   // SHIFT + 4
    {'%', SDL_SCANCODE_5, true, false},   // SHIFT + 5
    {'^', SDL_SCANCODE_6, true, false},   // SHIFT + 6
    {'&', SDL_SCANCODE_7, true, false},   // SHIFT + 7
    {'*', SDL_SCANCODE_8, true, false},   // SHIFT + 8
    {'(', SDL_SCANCODE_9, true, false},   // SHIFT + 9
    {')', SDL_SCANCODE_0, true, false},   // SHIFT + 0
    {'"', SDL_SCANCODE_APOSTROPHE, true, false},  // SHIFT + APOSTROPHE
    
    // Basic punctuation
    {' ', SDL_SCANCODE_SPACE, false, false}, // SPACE
    {',', SDL_SCANCODE_COMMA, false, false}, // COMMA
    {'.', SDL_SCANCODE_PERIOD, false, false}, // PERIOD
    {'/', SDL_SCANCODE_SLASH, false, false}, // SLASH
    {';', SDL_SCANCODE_SEMICOLON, false, false}, // SEMICOLON
    {'\'', SDL_SCANCODE_APOSTROPHE, false, false}, // APOSTROPHE
    {'-', SDL_SCANCODE_MINUS, false, false}, // MINUS
    {'=', SDL_SCANCODE_EQUALS, false, false}, // EQUALS
    {'[', SDL_SCANCODE_LEFTBRACKET, false, false}, // LEFT BRACKET
    {']', SDL_SCANCODE_RIGHTBRACKET, false, false}, // RIGHT BRACKET
    {'\\', SDL_SCANCODE_BACKSLASH, false, false}, // BACKSLASH
    
    // Special keys (handled separately)
    {'\n', SDL_SCANCODE_RETURN, false, false}, // ENTER
    {'\t', SDL_SCANCODE_TAB, false, false},    // TAB
};

static const size_t char_map_size = sizeof(char_map) / sizeof(char_map[0]);

// Macro action types
enum MacroActionType {
    MACRO_KEY,    // Regular key press/release
    MACRO_WAIT    // Wait/pause
};

// Macro action structure
struct MacroAction {
    MacroActionType type;
    uint32_t value;      // SDL scancode for MACRO_KEY, milliseconds for MACRO_WAIT
    bool needs_shift;
    bool needs_ctrl;
};

// Macro lookup table
static const std::unordered_map<std::string, MacroAction> macro_map = {
    // Special keys
    {"ENTER", {MACRO_KEY, SDL_SCANCODE_RETURN, false, false}},
    {"RETURN", {MACRO_KEY, SDL_SCANCODE_RETURN, false, false}},
    {"TAB", {MACRO_KEY, SDL_SCANCODE_TAB, false, false}},
    {"ESCAPE", {MACRO_KEY, SDL_SCANCODE_ESCAPE, false, false}},
    {"ESC", {MACRO_KEY, SDL_SCANCODE_ESCAPE, false, false}},
    {"SPACE", {MACRO_KEY, SDL_SCANCODE_SPACE, false, false}},
    {"BACKSPACE", {MACRO_KEY, SDL_SCANCODE_BACKSPACE, false, false}},
    {"BS", {MACRO_KEY, SDL_SCANCODE_BACKSPACE, false, false}},
    {"DELETE", {MACRO_KEY, SDL_SCANCODE_DELETE, false, false}},
    {"DEL", {MACRO_KEY, SDL_SCANCODE_DELETE, false, false}},
    
    // Arrow keys
    {"UP", {MACRO_KEY, SDL_SCANCODE_UP, false, false}},
    {"DOWN", {MACRO_KEY, SDL_SCANCODE_DOWN, false, false}},
    {"LEFT", {MACRO_KEY, SDL_SCANCODE_LEFT, false, false}},
    {"RIGHT", {MACRO_KEY, SDL_SCANCODE_RIGHT, false, false}},
    {"CRSR-UP", {MACRO_KEY, SDL_SCANCODE_UP, false, false}},
    {"CRSR-DOWN", {MACRO_KEY, SDL_SCANCODE_DOWN, false, false}},
    {"CRSR-LEFT", {MACRO_KEY, SDL_SCANCODE_LEFT, false, false}},
    {"CRSR-RIGHT", {MACRO_KEY, SDL_SCANCODE_RIGHT, false, false}},
    
    // Function keys
    {"F1", {MACRO_KEY, SDL_SCANCODE_F1, false, false}},
    {"F2", {MACRO_KEY, SDL_SCANCODE_F2, false, false}},
    {"F3", {MACRO_KEY, SDL_SCANCODE_F3, false, false}},
    {"F4", {MACRO_KEY, SDL_SCANCODE_F4, false, false}},
    {"F5", {MACRO_KEY, SDL_SCANCODE_F5, false, false}},
    {"F6", {MACRO_KEY, SDL_SCANCODE_F6, false, false}},
    {"F7", {MACRO_KEY, SDL_SCANCODE_F7, false, false}},
    {"F8", {MACRO_KEY, SDL_SCANCODE_F8, false, false}},
    
    // Home/End/Page keys
    {"HOME", {MACRO_KEY, SDL_SCANCODE_HOME, false, false}},
    {"END", {MACRO_KEY, SDL_SCANCODE_END, false, false}},
    {"CLR", {MACRO_KEY, SDL_SCANCODE_HOME, true, false}},  // SHIFT+HOME for clear screen
    {"INST-DEL", {MACRO_KEY, SDL_SCANCODE_BACKSPACE, false, false}},
    
    // PETSCII color codes (using CTRL combinations)
    {"BLACK", {MACRO_KEY, SDL_SCANCODE_2, false, true}},    // CTRL+2
    {"WHITE", {MACRO_KEY, SDL_SCANCODE_9, false, true}},    // CTRL+9
    {"RED", {MACRO_KEY, SDL_SCANCODE_3, false, true}},      // CTRL+3
    {"CYAN", {MACRO_KEY, SDL_SCANCODE_4, false, true}},     // CTRL+4
    {"PURPLE", {MACRO_KEY, SDL_SCANCODE_5, false, true}},   // CTRL+5
    {"GREEN", {MACRO_KEY, SDL_SCANCODE_6, false, true}},    // CTRL+6
    {"BLUE", {MACRO_KEY, SDL_SCANCODE_7, false, true}},     // CTRL+7
    {"YELLOW", {MACRO_KEY, SDL_SCANCODE_8, false, true}},   // CTRL+8
    
    // PETSCII symbols (correct key combinations)
    {"HEART", {MACRO_KEY, SDL_SCANCODE_S, true, false}},    // SHIFT+S
    {"SPADE", {MACRO_KEY, SDL_SCANCODE_A, true, false}},    // SHIFT+A
    {"CLUB", {MACRO_KEY, SDL_SCANCODE_X, true, false}},     // SHIFT+X
    {"DIAMOND", {MACRO_KEY, SDL_SCANCODE_Z, true, false}}   // SHIFT+Z
    
    // Note: Wait/pause macros (_123, _1.5, etc.) are parsed dynamically
};

// Direct lookup table for O(1) character mapping
static CharacterMapping char_lookup[128];
static bool lookup_initialized = false;

// Initialize the lookup table once
static void init_char_lookup() {
    if (lookup_initialized) return;
    
    // Initialize all entries as "no mapping"
    for (int i = 0; i < 128; i++) {
        char_lookup[i].ascii_char = (char)i;
        char_lookup[i].scancode = 0;  // 0 means no mapping
        char_lookup[i].needs_shift = false;
        char_lookup[i].needs_ctrl = false;
    }
    
    // Populate from our mapping data
    for (size_t i = 0; i < char_map_size; i++) {
        char c = char_map[i].ascii_char;
        unsigned char uc = (unsigned char)c;
        if (uc < 128) {
            char_lookup[uc] = char_map[i];
        }
    }
    
    lookup_initialized = true;
}

// Parse macro from input string starting at given position
// Returns number of characters consumed (not including delimiters)
int parse_macro(const std::string& input, size_t start_pos, 
                InputEventQueue* queue, int typing_rate_ms,
                bool& shift_down, bool& ctrl_down) {
    if (start_pos >= input.length()) {
        return 0;
    }
    
    // Find the end of the macro (invalid characters for macro names)
    size_t end_pos = start_pos;
    while (end_pos < input.length()) {
        char c = input[end_pos];
        // Valid macro characters: letters, numbers, underscore, hyphen, period
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            break;
        }
        end_pos++;
    }
    
    if (end_pos == start_pos) {
        // No valid macro characters found
        return 0;
    }
    
    // Extract macro name
    std::string macro_name = input.substr(start_pos, end_pos - start_pos);
    
    // Convert to uppercase for lookup
    for (char& c : macro_name) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
    }
    
    X16_LOG_DEBUG("Parsing macro: '%s'\n", macro_name.c_str());
    
    // Check for dynamic wait macro (starts with underscore)
    if (macro_name.length() > 1 && macro_name[0] == '_') {
        std::string time_str = macro_name.substr(1);  // Remove the underscore
        
        // Parse the numeric value
        char* endptr;
        double time_value = strtod(time_str.c_str(), &endptr);
        
        if (endptr == time_str.c_str() || time_value < 0) {
            X16_LOG_WARN("WARNING: Invalid wait time in macro '%s'\n", macro_name.c_str());
            return end_pos - start_pos;
        }
        
        // Convert to milliseconds
        uint32_t wait_ms;
        if (time_str.find('.') != std::string::npos) {
            // Decimal value - treat as seconds, convert to milliseconds
            wait_ms = (uint32_t)(time_value * 1000.0);
        } else {
            // Integer value - treat as milliseconds
            wait_ms = (uint32_t)time_value;
        }
        
        // Add wait event
        queue->add_event(INPUT_TYPE_WAIT, 0, 0, wait_ms);
        
        X16_LOG_DEBUG("Added dynamic WAIT event: %dms (from '%s')\n", wait_ms, macro_name.c_str());
        return end_pos - start_pos;
    }
    
    // Look up macro in static table
    auto it = macro_map.find(macro_name);
    if (it == macro_map.end()) {
        X16_LOG_WARN("WARNING: Unknown macro '%s'\n", macro_name.c_str());
        return end_pos - start_pos;  // Consume the characters anyway
    }
    
    const MacroAction& action = it->second;
    
    if (action.type == MACRO_WAIT) {
        // Add wait event
        queue->add_event(INPUT_TYPE_WAIT, 0, 0, action.value);
        
        X16_LOG_DEBUG("Added WAIT event: %dms\n", action.value);
    } else if (action.type == MACRO_KEY) {
        // Handle SHIFT key state changes
        if (action.needs_shift != shift_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LSHIFT, action.needs_shift ? 1 : 0, KEY_EVENT_MIN_DELAY_MS);
            shift_down = action.needs_shift;
        }
        
        // Handle CTRL key state changes (using LALT for Commodore key)
        if (action.needs_ctrl != ctrl_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LALT, action.needs_ctrl ? 1 : 0, KEY_EVENT_MIN_DELAY_MS);
            ctrl_down = action.needs_ctrl;
        }
        
        // Add key press
        queue->add_event(INPUT_TYPE_KEYBOARD, action.value, 1, typing_rate_ms);
        
        // Add key release
        queue->add_event(INPUT_TYPE_KEYBOARD, action.value, 0, KEY_EVENT_UP_DELAY_MS);
        
        X16_LOG_DEBUG("Added key macro: scancode=%d, shift=%d, ctrl=%d\n", 
                     action.value, action.needs_shift, action.needs_ctrl);
    }
    
    return end_pos - start_pos;
}

// Translate ASCII string to input events using proper timing algorithm
bool translate_ascii_to_events(const std::string& input, InputEventQueue* queue, int typing_rate_ms, DisplayMode mode) {
    if (!queue) {
        X16_LOG_ERROR("ERROR: NULL queue passed to translate_ascii_to_events\n");
        return false;
    }
    
    // Initialize lookup table once at start of function
    if (!lookup_initialized) {
        init_char_lookup();
    }
    
    printf("DEBUG: translate_ascii_to_events called with input: \"%s\"\n", input.c_str());
    fflush(stdout);
    
    X16_LOG_INFO("Translating ASCII input: \"%s\" (mode: %s)\n", 
               input.c_str(), 
               (mode == DisplayMode::PETSCII) ? "PETSCII" : "ASCII");
    
    // Timing algorithm state
    bool shift_down = false;
    bool ctrl_down = false;
    
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        
        // Handle backtick-delimited macros
        if (c == '`') {
            // Skip opening backtick
            i++;
            if (i >= input.length()) {
                X16_LOG_WARN("WARNING: Unterminated macro at end of input\n");
                break;
            }
            
            // Parse macro
            int consumed = parse_macro(input, i, queue, typing_rate_ms, shift_down, ctrl_down);
            if (consumed > 0) {
                i += consumed - 1;  // -1 because loop will increment
                
                // Look for closing backtick
                if (i + 1 < input.length() && input[i + 1] == '`') {
                    i++;  // Skip closing backtick
                } else {
                    X16_LOG_WARN("WARNING: Missing closing backtick for macro\n");
                }
            } else {
                X16_LOG_WARN("WARNING: Empty or invalid macro\n");
            }
            continue;
        }
        
        // Handle escape sequences
        // NOTE: JSON will convert /n to actual newline octet codes so do not think this
        // is needed here. Don't care about tab.
        /*if (c == '\\' && i + 1 < input.length()) {
            char next = input[i + 1];
            if (next == 'n') {
                c = '\n';  // Newline
                i++;
            } else if (next == 't') {
                // Tab
                c = '\t';
                i++;
            }
        }*/
        
        // Direct inline lookup - no function call overhead
        unsigned char uc = (unsigned char)c;
        if (uc >= 128 || char_lookup[uc].scancode == 0) {
            //X16_LOG_WARN("WARNING: No mapping found for character '%c' (0x%02X)\n", c, (unsigned char)c);
            continue;
        }
        
        const CharacterMapping* mapping = &char_lookup[uc];
        
        // Handle SHIFT key state changes
        bool needs_shift = mapping->needs_shift || 
            (mode == DisplayMode::ASCII && (c >= 'A' && c <= 'Z'));
        if (needs_shift != shift_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LSHIFT, needs_shift ? 1 : 0, KEY_EVENT_MIN_DELAY_MS);
            shift_down = needs_shift;
        }

        // Handle CTRL key state changes (using LALT for Commodore key)
        if (mapping->needs_ctrl != ctrl_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LALT, mapping->needs_ctrl ? 1 : 0, KEY_EVENT_MIN_DELAY_MS);
            ctrl_down = mapping->needs_ctrl;
        }
        
        // Add the main key press (DOWN)
        queue->add_event(INPUT_TYPE_KEYBOARD, mapping->scancode, 1, typing_rate_ms);
        
        // Add the main key release (UP)
        queue->add_event(INPUT_TYPE_KEYBOARD, mapping->scancode, 0, KEY_EVENT_UP_DELAY_MS);
    }
    
    // Release any remaining modifier keys at the end
    if (shift_down) {
        queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LSHIFT, 0, KEY_EVENT_MIN_DELAY_MS);
    }

    if (ctrl_down) {
        queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LALT, 0, KEY_EVENT_MIN_DELAY_MS);
    }
    
    X16_LOG_INFO("Generated %zu input events for ASCII translation\n", queue->size());
    return true;
}

// Detect current display mode (placeholder - needs implementation)
DisplayMode detect_display_mode() {
    // For now, hard code PETSCII mode (X16 default)
    // TODO: Implement actual detection by checking X16 display state
    return DisplayMode::PETSCII;
}

// Timer-based event processor - processes all pending input queues
extern "C" void process_input_event_queues() {    
    if (!g_processing_active || g_pending_queues.empty()) {
        return;
    }

    uint32_t current_time = SDL_GetTicks();
    g_elapsed_time += current_time - g_last_event_time;
    g_last_event_time = current_time;
    
    // Process current queue
    while (!g_pending_queues.empty()) {

        InputEventQueue* current_queue = g_pending_queues[0];
        
        // Process all events in current queue whose wait time has elapsed
        for (;;) {

            if (g_current_event_index >= current_queue->size()) {
                g_pending_queues.erase(g_pending_queues.begin());
                delete current_queue;
                g_current_event_index = 0;
                break; // Move to next queue
            }

            const InputEvent& event = (*current_queue)[g_current_event_index];
            
            if (g_elapsed_time < event.wait_ms) {
                // Not enough time has passed for this event
                return;
            }

            g_elapsed_time -= event.wait_ms; // Deduct elapsed time
            
            // Process this event
            if (event.type == INPUT_TYPE_KEYBOARD) {
                // Send keyboard event directly to emulator
                handle_keyboard(event.is_down != 0, 0, (SDL_Scancode)event.code);
            } else if (event.type == INPUT_TYPE_JOYSTICK) {
                // TODO: Handle joystick events when implemented
            } else if (event.type == INPUT_TYPE_WAIT) {
                // WAIT events don't perform any action, just consume time
            }
            
            // Move to next event
            g_current_event_index++;
        }
    }

    g_processing_active = false; // No more queues to process
    X16_LOG_INFO("All input queues processed\n");
}

// Submit input queue to the processing system
void submit_input_queue(InputEventQueue* queue) {
    if (!queue) {
        X16_LOG_ERROR("ERROR: NULL queue passed to submit_input_queue\n");
        return;
    }
    
    X16_LOG_INFO("Submitting input queue with %zu events to processing system\n", queue->size());
    
    // Add queue to pending list
    g_pending_queues.push_back(queue);
    
    // Start processing if not already active
    if (!g_processing_active || g_pending_queues.size() == 1) {
        g_processing_active = true;
        g_current_event_index = 0;
        g_elapsed_time = 0;
        g_last_event_time = SDL_GetTicks();
        
        X16_LOG_INFO("Started input queue processing system\n");
    }
}

// Joystick translation (placeholder)
bool translate_joystick_to_events(const std::string& input, InputEventQueue* queue, int joystick_num) {
    // TODO: Implement joystick command translation
    X16_LOG_INFO("Joystick translation not yet implemented\n");
    return false;
}

// Legacy compatibility functions (simplified implementations)
ProcessedKeyboardData process_keyboard_input(const std::string& input, KeyboardMode mode) {
    ProcessedKeyboardData result;
    result.success = false;
    result.error_message = "Legacy function - use new input event system";
    return result;
}

ProcessedKeyboardData process_joystick_input(const std::string& input, int joystick_num) {
    ProcessedKeyboardData result;
    result.success = false;
    result.error_message = "Legacy function - use new input event system";
    return result;
}

std::string keyboard_mode_to_string(KeyboardMode mode) {
    switch (mode) {
        case KeyboardMode::ASCII: return "ascii";
        case KeyboardMode::PETSCII: return "petscii";
        case KeyboardMode::RAW: return "raw";
        default: return "unknown";
    }
}

KeyboardMode string_to_keyboard_mode(const std::string& mode_str) {
    if (mode_str == "ascii") return KeyboardMode::ASCII;
    if (mode_str == "petscii") return KeyboardMode::PETSCII;
    if (mode_str == "raw") return KeyboardMode::RAW;
    return KeyboardMode::ASCII; // default
}
