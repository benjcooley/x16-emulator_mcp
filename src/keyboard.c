#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "glue.h"
#include "i2c.h"
#include "keyboard.h"

// MCP keyboard input queue to prevent buffer overflow
#define MCP_KEYBOARD_QUEUE_SIZE 4096
static char mcp_keyboard_queue[MCP_KEYBOARD_QUEUE_SIZE];
static int mcp_queue_head = 0;
static int mcp_queue_tail = 0;
static uint32_t last_key_inject_time = 0;
static const uint32_t KEY_INJECT_DELAY_MS = 10; // 10ms between characters (100 chars/sec)

// Forward declaration
static uint8_t ascii_to_x16_keycode(char c);

// Check if MCP keyboard queue has data
static bool mcp_keyboard_queue_has_data(void) {
	return mcp_queue_head != mcp_queue_tail;
}

// Add text to MCP keyboard queue with bounds checking
static bool mcp_keyboard_queue_add_text(const char* text) {
	if (!text) {
		return false; // Null pointer check
	}
	
	int text_len = strlen(text);
	if (text_len == 0) {
		return true; // Empty string is valid
	}
	
	// Calculate available space with proper bounds checking
	int available_space = (MCP_KEYBOARD_QUEUE_SIZE + mcp_queue_tail - mcp_queue_head - 1) % MCP_KEYBOARD_QUEUE_SIZE;
	
	if (text_len > available_space) {
		return false; // Not enough space
	}
	
	// Add characters with bounds checking
	for (int i = 0; i < text_len; i++) {
		mcp_keyboard_queue[mcp_queue_head] = text[i];
		mcp_queue_head = (mcp_queue_head + 1) % MCP_KEYBOARD_QUEUE_SIZE;
		
		// Sanity check - should never happen with proper calculation above
		if (mcp_queue_head == mcp_queue_tail) {
			// Queue full, this shouldn't happen but handle gracefully
			return false;
		}
	}
	
	return true;
}

// Get next character from MCP keyboard queue
static char mcp_keyboard_queue_get_next(void) {
	if (mcp_queue_head == mcp_queue_tail) {
		return 0; // Queue empty
	}
	
	char c = mcp_keyboard_queue[mcp_queue_tail];
	mcp_queue_tail = (mcp_queue_tail + 1) % MCP_KEYBOARD_QUEUE_SIZE;
	return c;
}

// Process MCP keyboard queue with timing (call this from main loop)
void keyboard_process_mcp_queue(void) {
	// First, process the new timer-based input event queues
	process_input_event_queues();
	
	// Then process the legacy character-based queue for backward compatibility
	if (!mcp_keyboard_queue_has_data()) {
		return;
	}
	
	uint32_t current_time = SDL_GetTicks();
	if (current_time - last_key_inject_time < KEY_INJECT_DELAY_MS) {
		return; // Too soon to inject next character
	}
	
	// Check if keyboard buffer has space (leave some room for press+release)
	extern uint8_t kbd_head, kbd_tail;
	int kbd_used = (16 + kbd_head - kbd_tail) % 16;
	if (kbd_used > 10) { // Don't fill more than 10/16 slots (need space for press+release)
		return;
	}
	
	char c = mcp_keyboard_queue_get_next();
	if (c != 0) {
		// Convert ASCII character to X16 keycode
		uint8_t keycode = ascii_to_x16_keycode(c);
		if (keycode != 0) {
			// Send key press event
			i2c_kbd_buffer_add(keycode);
			// Send key release event (set bit 7) - this simulates a complete keypress
			i2c_kbd_buffer_add(keycode | 0x80);
			last_key_inject_time = current_time;
		}
	}
}

// MCP server keyboard wrapper functions
void keyboard_add_event(uint8_t key, bool pressed) {
	if (pressed) {
		i2c_kbd_buffer_add(key);
	} else {
		i2c_kbd_buffer_add(key | 0x80); // Set release bit
	}
}

void keyboard_add_char(char c) {
	// For single characters, add directly to avoid delay
	i2c_kbd_buffer_add((uint8_t)c);
}

// Add text with proper queuing and timing
bool keyboard_add_text(const char* text) {
	return mcp_keyboard_queue_add_text(text);
}

// Convert ASCII character to X16 keycode with comprehensive support
static uint8_t ascii_to_x16_keycode(char c) {
	// Convert lowercase to uppercase (Commodore convention)
	if (c >= 'a' && c <= 'z') {
		c = c - 'a' + 'A';
	}
	
	switch (c) {
		// Letters A-Z (uppercase)
		case 'A': return 31;
		case 'B': return 50;
		case 'C': return 48;
		case 'D': return 33;
		case 'E': return 19;
		case 'F': return 34;
		case 'G': return 35;
		case 'H': return 36;
		case 'I': return 24;
		case 'J': return 37;
		case 'K': return 38;
		case 'L': return 39;
		case 'M': return 52;
		case 'N': return 51;
		case 'O': return 25;
		case 'P': return 26;
		case 'Q': return 17;
		case 'R': return 20;
		case 'S': return 32;
		case 'T': return 21;
		case 'U': return 23;
		case 'V': return 49;
		case 'W': return 18;
		case 'X': return 47;
		case 'Y': return 22;
		case 'Z': return 46;
		
		// Numbers 0-9
		case '0': return 11;
		case '1': return 2;
		case '2': return 3;
		case '3': return 4;
		case '4': return 5;
		case '5': return 6;
		case '6': return 7;
		case '7': return 8;
		case '8': return 9;
		case '9': return 10;
		
		// Basic punctuation and symbols (directly available)
		case ' ': return 61;  // Space
		case '\'': return 41; // Apostrophe/Quote
		case ',': return 53;  // Comma
		case '-': return 12;  // Minus/Hyphen
		case '.': return 54;  // Period
		case '/': return 55;  // Slash
		case ';': return 40;  // Semicolon
		case '=': return 13;  // Equals
		case '[': return 27;  // Left bracket
		case '\\': return 29; // Backslash
		case ']': return 28;  // Right bracket
		case '`': return 1;   // Grave/backtick
		
		// Control characters
		case '\n': return 43; // Enter/Return (newline)
		case '\r': return 43; // Enter/Return (carriage return)
		case '\t': return 16; // Tab
		case '\b': return 15; // Backspace
		
		// Characters that require SHIFT (these are the shifted versions)
		// Note: X16 keyboard may not support all of these directly
		// For now, we'll ignore unsupported shifted characters
		case '!': return 0;   // Shift+1 - not directly mappable
		case '@': return 0;   // Shift+2 - not directly mappable  
		case '#': return 0;   // Shift+3 - not directly mappable
		case '$': return 0;   // Shift+4 - not directly mappable
		case '%': return 0;   // Shift+5 - not directly mappable
		case '^': return 0;   // Shift+6 - not directly mappable
		case '&': return 0;   // Shift+7 - not directly mappable
		case '*': return 0;   // Shift+8 - not directly mappable
		case '(': return 0;   // Shift+9 - not directly mappable
		case ')': return 0;   // Shift+0 - not directly mappable
		case '_': return 0;   // Shift+- - not directly mappable
		case '+': return 0;   // Shift+= - not directly mappable
		case '{': return 0;   // Shift+[ - not directly mappable
		case '}': return 0;   // Shift+] - not directly mappable
		case '|': return 0;   // Shift+\ - not directly mappable
		case ':': return 0;   // Shift+; - not directly mappable
		case '"': return 0;   // Shift+' - not directly mappable
		case '<': return 0;   // Shift+, - not directly mappable
		case '>': return 0;   // Shift+. - not directly mappable
		case '?': return 0;   // Shift+/ - not directly mappable
		case '~': return 0;   // Shift+` - not directly mappable
		
		// Extended ASCII and other characters - ignore
		default:
			// For any character we don't recognize, return 0 (ignore)
			// This includes extended ASCII (128-255) and other Unicode
			return 0;
	}
}

// Get queue status
int keyboard_get_queue_size(void) {
	return (MCP_KEYBOARD_QUEUE_SIZE + mcp_queue_head - mcp_queue_tail) % MCP_KEYBOARD_QUEUE_SIZE;
}

#define EXTENDED_FLAG 0x100
// #define ESC_IS_BREAK /* if enabled, Esc sends Break/Pause key instead of Esc */

int
keynum_from_SDL_Scancode(SDL_Scancode scancode)
{
	switch (scancode) {
		case SDL_SCANCODE_GRAVE:
			return 1;
		case SDL_SCANCODE_BACKSPACE:
			return 15;
		case SDL_SCANCODE_TAB:
			return 16;
		case SDL_SCANCODE_CLEAR:
			return 0;
		case SDL_SCANCODE_RETURN:
			return 43;
		case SDL_SCANCODE_PAUSE:
			return 126;
		case SDL_SCANCODE_ESCAPE:
#ifdef ESC_IS_BREAK
			return 126;
#else
			return 110;
#endif
		case SDL_SCANCODE_SPACE:
			return 61;
		case SDL_SCANCODE_APOSTROPHE:
			return 41;
		case SDL_SCANCODE_COMMA:
			return 53;
		case SDL_SCANCODE_MINUS:
			return 12;
		case SDL_SCANCODE_PERIOD:
			return 54;
		case SDL_SCANCODE_SLASH:
			return 55;
		case SDL_SCANCODE_0:
			return 11;
		case SDL_SCANCODE_1:
			return 2;
		case SDL_SCANCODE_2:
			return 3;
		case SDL_SCANCODE_3:
			return 4;
		case SDL_SCANCODE_4:
			return 5;
		case SDL_SCANCODE_5:
			return 6;
		case SDL_SCANCODE_6:
			return 7;
		case SDL_SCANCODE_7:
			return 8;
		case SDL_SCANCODE_8:
			return 9;
		case SDL_SCANCODE_9:
			return 10;
		case SDL_SCANCODE_SEMICOLON:
			return 40;
		case SDL_SCANCODE_EQUALS:
			return 13;
		case SDL_SCANCODE_LEFTBRACKET:
			return 27;
		case SDL_SCANCODE_BACKSLASH:
			return 29;
		case SDL_SCANCODE_RIGHTBRACKET:
			return 28;
		case SDL_SCANCODE_A:
			return 31;
		case SDL_SCANCODE_B:
			return 50;
		case SDL_SCANCODE_C:
			return 48;
		case SDL_SCANCODE_D:
			return 33;
		case SDL_SCANCODE_E:
			return 19;
		case SDL_SCANCODE_F:
			return 34;
		case SDL_SCANCODE_G:
			return 35;
		case SDL_SCANCODE_H:
			return 36;
		case SDL_SCANCODE_I:
			return 24;
		case SDL_SCANCODE_J:
			return 37;
		case SDL_SCANCODE_K:
			return 38;
		case SDL_SCANCODE_L:
			return 39;
		case SDL_SCANCODE_M:
			return 52;
		case SDL_SCANCODE_N:
			return 51;
		case SDL_SCANCODE_O:
			return 25;
		case SDL_SCANCODE_P:
			return 26;
		case SDL_SCANCODE_Q:
			return 17;
		case SDL_SCANCODE_R:
			return 20;
		case SDL_SCANCODE_S:
			return 32;
		case SDL_SCANCODE_T:
			return 21;
		case SDL_SCANCODE_U:
			return 23;
		case SDL_SCANCODE_V:
			return 49;
		case SDL_SCANCODE_W:
			return 18;
		case SDL_SCANCODE_X:
			return 47;
		case SDL_SCANCODE_Y:
			return 22;
		case SDL_SCANCODE_Z:
			return 46;
		case SDL_SCANCODE_DELETE:
			return 76;
		case SDL_SCANCODE_UP:
			return 83;
		case SDL_SCANCODE_DOWN:
			return 84;
		case SDL_SCANCODE_RIGHT:
			return 89;
		case SDL_SCANCODE_LEFT:
			return 79;
		case SDL_SCANCODE_INSERT:
			return 75;
		case SDL_SCANCODE_HOME:
			return 80;
		case SDL_SCANCODE_END:
			return 81;
		case SDL_SCANCODE_PAGEUP:
			return 85;
		case SDL_SCANCODE_PAGEDOWN:
			return 86;
		case SDL_SCANCODE_F1:
			return 112;
		case SDL_SCANCODE_F2:
			return 113;
		case SDL_SCANCODE_F3:
			return 114;
		case SDL_SCANCODE_F4:
			return 115;
		case SDL_SCANCODE_F5:
			return 116;
		case SDL_SCANCODE_F6:
			return 117;
		case SDL_SCANCODE_F7:
			return 118;
		case SDL_SCANCODE_F8:
			return 119;
		case SDL_SCANCODE_F9:
			return 120;
		case SDL_SCANCODE_F10:
			return 121;
		case SDL_SCANCODE_F11:
			return 122;
		case SDL_SCANCODE_F12:
			return 123;
		case SDL_SCANCODE_SCROLLLOCK:
			return 125;
		case SDL_SCANCODE_RSHIFT:
			return 57;
		case SDL_SCANCODE_LSHIFT:
			return 44;
		case SDL_SCANCODE_CAPSLOCK:
			return 30;
		case SDL_SCANCODE_LCTRL:
			return 58;
		case SDL_SCANCODE_RCTRL:
			return 64;
		case SDL_SCANCODE_LALT:
			return 60;
		case SDL_SCANCODE_RALT:
			return 62;
		case SDL_SCANCODE_LGUI:
			return 59;
		case SDL_SCANCODE_RGUI:
			return 63;
		case SDL_SCANCODE_APPLICATION: // Menu
			return 65;
		case SDL_SCANCODE_NONUSBACKSLASH:
			return 45;
		case SDL_SCANCODE_KP_ENTER:
			return 108;
		case SDL_SCANCODE_KP_0:
			return 99;
		case SDL_SCANCODE_KP_1:
			return 93;
		case SDL_SCANCODE_KP_2:
			return 98;
		case SDL_SCANCODE_KP_3:
			return 103;
		case SDL_SCANCODE_KP_4:
			return 92;
		case SDL_SCANCODE_KP_5:
			return 97;
		case SDL_SCANCODE_KP_6:
			return 102;
		case SDL_SCANCODE_KP_7:
			return 91;
		case SDL_SCANCODE_KP_8:
			return 96;
		case SDL_SCANCODE_KP_9:
			return 101;
		case SDL_SCANCODE_KP_PERIOD:
			return 104;
		case SDL_SCANCODE_KP_PLUS:
			return 106;
		case SDL_SCANCODE_KP_MINUS:
			return 105;
		case SDL_SCANCODE_KP_MULTIPLY:
			return 100;
		case SDL_SCANCODE_KP_DIVIDE:
			return 95;
		case SDL_SCANCODE_NUMLOCKCLEAR:
			return 90;
		case SDL_SCANCODE_INTERNATIONAL1:
			return 56;
		default:
			return 0;
	}
}

void
handle_keyboard(bool down, SDL_Keycode sym, SDL_Scancode scancode)
{
	int keynum = keynum_from_SDL_Scancode(scancode);
	
	if (keynum == 0) return;

	if (down) {
		if (log_keyboard) {
			printf("DOWN 0x%02X\n", scancode);
			fflush(stdout);
		}

		if (keynum & EXTENDED_FLAG) {
			i2c_kbd_buffer_add(0x7f);
		}
		i2c_kbd_buffer_add(keynum & 0xff);
	} else {
		if (log_keyboard) {
			printf("UP   0x%02X\n", scancode);
			fflush(stdout);
		}

		keynum = keynum | 0b10000000;
		if (keynum & EXTENDED_FLAG) {
			i2c_kbd_buffer_add(0xff);
		}
		i2c_kbd_buffer_add(keynum & 0xff);
	}
}
