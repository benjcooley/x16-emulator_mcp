void handle_keyboard(bool down, SDL_Keycode sym, SDL_Scancode scancode);

// MCP server keyboard wrapper functions
void keyboard_add_event(uint8_t key, bool pressed);
void keyboard_add_char(char c);
bool keyboard_add_text(const char* text);
int keyboard_get_queue_size(void);
void keyboard_process_mcp_queue(void);
