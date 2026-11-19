#ifndef QJS_STUDIO_EDITOR_H
#define QJS_STUDIO_EDITOR_H

#include <windows.h>

typedef struct EditorViewState {
    LRESULT position;
    LRESULT anchor;
    int first_visible_line;
    int x_offset;
} EditorViewState;

int editor_load_components(void);
void editor_unload_components(void);
HWND editor_create(HWND parent, int control_id);
int editor_open(HWND editor, const char *path);
int editor_save(HWND editor);
int editor_save_as(HWND editor, const char *path);
void editor_reset(HWND editor);
int editor_is_modified(HWND editor);
int editor_current_line(HWND editor);
int editor_selected_text(HWND editor, char *text, size_t capacity);
int editor_symbol_at_caret(HWND editor, char *symbol, size_t capacity);
void editor_goto_line(HWND editor, int line);
void editor_update_line_number_margin(HWND editor);
void editor_apply_semantic_colors(HWND editor);
void editor_update_brace_highlight(HWND editor);
void editor_handle_newline(HWND editor);
void editor_handle_closing_brace(HWND editor);
void editor_get_view_state(HWND editor, EditorViewState *state);
void editor_set_view_state(HWND editor, const EditorViewState *state);
int editor_collect_breakpoints(HWND editor, int *lines, int capacity);
void editor_toggle_breakpoint(HWND editor, int line);
void editor_set_execution_line(HWND editor, int line);
void editor_clear_execution_line(HWND editor);
void editor_toggle_bookmark(HWND editor);
int editor_next_bookmark(HWND editor, int forward);
void editor_clear_bookmarks(HWND editor);
const char *editor_path(void);

#endif
