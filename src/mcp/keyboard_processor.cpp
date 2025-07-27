#include "keyboard_processor.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
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
static size_t g_current_queue_index = 0;
static size_t g_current_event_index = 0;
static uint32_t g_last_event_time = 0;
static bool g_processing_active = false;


// Timing constants (in milliseconds)
#define KEY_EVENT_MIN_DELAY_MS  1   // Standard delay for each key event (down/up)

// InputEventQueue implementation using std::vector
InputEventQueue::InputEventQueue(size_t initial_capacity) {
    events.reserve(initial_capacity);
}

InputEventQueue::~InputEventQueue() {
    // std::vector handles cleanup automatically
}

bool InputEventQueue::add_event(uint8_t type, uint8_t code, uint8_t is_down, uint32_t wait_ms) {
    events.emplace_back(InputEvent{type, code, is_down, wait_ms});
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
    uint8_t scancode;
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
    
    X16_LOG_INFO("Translating ASCII input: \"%s\" (mode: %s)\n", 
               input.c_str(), 
               (mode == DisplayMode::PETSCII) ? "PETSCII" : "ASCII");
    
    // Timing algorithm state
    bool shift_down = false;
    bool ctrl_down = false;
    int next_key_delay = 0;
    
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        
        // Handle escape sequences
        if (c == '\\' && i + 1 < input.length()) {
            char next = input[i + 1];
            if (next == 'n') {
                c = '\n';  // Newline
                i++;
            } else if (next == 't') {
                // Tab
                c = '\t';
                i++;
            }
        }
        
        // Direct inline lookup - no function call overhead
        unsigned char uc = (unsigned char)c;
        if (uc >= 128 || char_lookup[uc].scancode == 0) {
            X16_LOG_WARN("WARNING: No mapping found for character '%c' (0x%02X)\n", c, (unsigned char)c);
            continue;
        }
        
        const CharacterMapping* mapping = &char_lookup[uc];
        
        int start_delay = next_key_delay;  // Save initial delay for this character
        int total_delay = 0;

        // Handle SHIFT key state changes
        bool needs_shift = mapping->needs_shift || 
            (mode == DisplayMode::ASCII && (c >= 'A' && c <= 'Z'));
        if (needs_shift != shift_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LSHIFT, needs_shift ? 1 : 0, next_key_delay);
            total_delay += next_key_delay;
            next_key_delay = KEY_EVENT_MIN_DELAY_MS;  // First event for this char has no delay
            shift_down = needs_shift;
        }

        // Handle SHIFT key state changes
        if (mapping->needs_ctrl != ctrl_down) {
            queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LCTRL, mapping->needs_ctrl ? 1 : 0, next_key_delay);
            total_delay += next_key_delay;
            next_key_delay = KEY_EVENT_MIN_DELAY_MS;
            ctrl_down = mapping->needs_ctrl;
        }
        
        // Add the main key press (DOWN)
        queue->add_event(INPUT_TYPE_KEYBOARD, mapping->scancode, 1, next_key_delay);
        total_delay += next_key_delay;
        next_key_delay = KEY_EVENT_MIN_DELAY_MS; 
        
        // Add the main key release (UP)
        queue->add_event(INPUT_TYPE_KEYBOARD, mapping->scancode, 0, next_key_delay);
        total_delay += next_key_delay;
        next_key_delay = KEY_EVENT_MIN_DELAY_MS;
        
        // Calculate delay for next character
        next_key_delay = typing_rate_ms - (total_delay - start_delay);
        if (next_key_delay < KEY_EVENT_MIN_DELAY_MS) {
            next_key_delay = KEY_EVENT_MIN_DELAY_MS;  // Ensure minimum delay
        }
    }
    
    // Release any remaining modifier keys at the end
    if (shift_down) {
        queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LSHIFT, 0, KEY_EVENT_MIN_DELAY_MS);
    }

    if (ctrl_down) {
        queue->add_event(INPUT_TYPE_KEYBOARD, SDL_SCANCODE_LCTRL, 0, KEY_EVENT_MIN_DELAY_MS);
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
    uint32_t elapsed_time = current_time - g_last_event_time;
    
    // Process current queue
    while (g_current_queue_index < g_pending_queues.size()) {
        InputEventQueue* current_queue = g_pending_queues[g_current_queue_index];
        
        // Process all events in current queue whose wait time has elapsed
        bool processed_any = false;
        while (g_current_event_index < current_queue->size()) {
            const InputEvent& event = (*current_queue)[g_current_event_index];
            
            if (elapsed_time < event.wait_ms) {
                // Not enough time has passed for this event
                break;
            }
            
            // Process this event
            if (event.type == INPUT_TYPE_KEYBOARD) {
                // Send keyboard event directly to emulator
                handle_keyboard(event.is_down != 0, 0, (SDL_Scancode)event.code);
                
                X16_LOG_DEBUG("Processed keyboard event: scancode=%d, down=%d, wait=%dms\n", 
                           event.code, event.is_down, event.wait_ms);
            } else if (event.type == INPUT_TYPE_JOYSTICK) {
                // TODO: Handle joystick events when implemented
                X16_LOG_DEBUG("Joystick event processing not yet implemented\n");
            }
            
            // Move to next event
            g_current_event_index++;
            processed_any = true;
            
            // Update timing for next event
            g_last_event_time = current_time;
            elapsed_time = 0; // Reset for next event in same frame
        }
        
        // Check if current queue is finished
        if (g_current_event_index >= current_queue->size()) {
            // Queue is complete - clean it up and move to next
            X16_LOG_INFO("Completed processing queue %zu with %zu events\n", 
                       g_current_queue_index, current_queue->size());
            
            delete current_queue;
            g_current_queue_index++;
            g_current_event_index = 0;
            
            // Continue processing next queue in same frame if available
            continue;
        }
        
        // If we processed any events, we're done for this frame
        if (processed_any) {
            break;
        }
        
        // No events were ready, wait for next frame
        break;
    }
    
    // Check if all queues are processed
    if (g_current_queue_index >= g_pending_queues.size()) {
        // All queues processed - reset state
        g_pending_queues.clear();
        g_current_queue_index = 0;
        g_current_event_index = 0;
        g_processing_active = false;
        
        X16_LOG_INFO("All input queues processed - system idle\n");
    }
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
    if (!g_processing_active) {
        g_processing_active = true;
        g_current_queue_index = 0;
        g_current_event_index = 0;
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
