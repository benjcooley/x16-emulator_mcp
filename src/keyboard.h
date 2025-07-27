// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

void handle_keyboard(bool down, SDL_Keycode sym, SDL_Scancode scancode);

// MCP server keyboard wrapper functions
void keyboard_add_event(uint8_t key, bool pressed);
void keyboard_add_char(char c);
bool keyboard_add_text(const char* text);
int keyboard_get_queue_size(void);
void keyboard_process_mcp_queue(void);

// Timer-based input event processor (implemented in keyboard_processor.cpp)
void process_input_event_queues(void);

#endif
