#ifndef QJS_STUDIO_INTERNAL_H
#define QJS_STUDIO_INTERNAL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <commctrl.h>

#include "debug_session.h"
#include "editor.h"
#include "project.h"
#include "symbol_search.h"

#include <stddef.h>

enum {
    ID_FILE_OPEN_PROJECT = 100,
    ID_FILE_NEW_PROJECT,
    ID_FILE_NEW_SCRIPT,
    ID_FILE_OPEN_SCRIPT,
    ID_FILE_SAVE,
    ID_FILE_SAVE_AS,
    ID_FILE_CLOSE_WORKSPACE,
    ID_FILE_EXIT,
    ID_EDIT_UNDO,
    ID_EDIT_REDO,
    ID_EDIT_CUT,
    ID_EDIT_COPY,
    ID_EDIT_PASTE,
    ID_EDIT_DELETE,
    ID_EDIT_SELECT_ALL,
    ID_EDIT_FIND,
    ID_EDIT_FIND_NEXT,
    ID_EDIT_FIND_IN_FILES,
    ID_EDIT_REPLACE,
    ID_EDIT_GO_TO_LINE,
    ID_EDIT_GO_TO_DEFINITION,
    ID_EDIT_NAVIGATE_BACK,
    ID_EDIT_NAVIGATE_FORWARD,
    ID_EDIT_AUTOCOMPLETE,
    ID_EDIT_TOGGLE_BOOKMARK,
    ID_EDIT_NEXT_BOOKMARK,
    ID_EDIT_PREVIOUS_BOOKMARK,
    ID_EDIT_CLEAR_BOOKMARKS,
    ID_INSERT_FILE_AS_TEXT,
    ID_DEBUG_START,
    ID_DEBUG_STOP,
    ID_DEBUG_BREAKPOINT,
    ID_DEBUG_STEP_OVER,
    ID_DEBUG_STEP_INTO,
    ID_DEBUG_STEP_OUT,
    ID_VIEW_OUTPUT,
    ID_VIEW_CALLSTACK,
    ID_VIEW_LOCALS,
    ID_VIEW_WATCH,
    ID_VIEW_WORKSPACE,
    ID_VIEW_TOOLBAR,
    ID_VIEW_WIZARDBAR,
    ID_VIEW_OUTPUT_WINDOW,
    ID_VIEW_STATUS_BAR,
    ID_PROJECT_NEW_FOLDER,
    ID_PROJECT_ADD_FILES,
    ID_PROJECT_RENAME,
    ID_PROJECT_REMOVE,
    ID_PROJECT_REFRESH,
    ID_PROJECT_PROPERTIES,
    ID_PROJECT_OPEN_ITEM,
    ID_TOOLS_ADD_WATCH,
    ID_HELP_ABOUT,
    ID_DOCUMENT_CLOSE,
    ID_TREE = 200,
    ID_DOCUMENT_BACKGROUND,
    ID_EDITOR,
    ID_BOTTOM_TABS,
    ID_OUTPUT,
    ID_CALLSTACK,
    ID_LOCALS,
    ID_WATCH_LIST,
    ID_TOOLBAR,
    ID_TOOLBAR2,
    ID_WIZARD_SCOPE,
    ID_WIZARD_MEMBER,
    ID_WIZARD_ACTION,
    ID_WORKSPACE_CLOSE,
    ID_LEFT_TABS,
    ID_STATUS,
    ID_GLOBAL_FIND_EDIT,
    ID_GLOBAL_FIND_BUTTON
};

enum { PAGE_OUTPUT, PAGE_CALLSTACK, PAGE_LOCALS, PAGE_WATCH };
enum { PAGE_CLASS_VIEW, PAGE_FILE_VIEW };
enum { SPLITTER_NONE, SPLITTER_VERTICAL, SPLITTER_HORIZONTAL };
enum {
    TREE_IMAGE_WORKSPACE,
    TREE_IMAGE_FOLDER,
    TREE_IMAGE_FILE,
    TREE_IMAGE_CLASS,
    TREE_IMAGE_FUNCTION,
    TREE_IMAGE_VARIABLE
};

typedef enum TreeTargetKind {
    TREE_TARGET_WORKSPACE,
    TREE_TARGET_PROJECT,
    TREE_TARGET_GROUP,
    TREE_TARGET_DIRECTORY,
    TREE_TARGET_FILE,
    TREE_TARGET_SYMBOL
} TreeTargetKind;

#define DOCK_LEFT_MARGIN 7
#define SPLITTER_SIZE 5
#define MIN_TREE_WIDTH 100
#define MIN_EDITOR_WIDTH 280
#define MIN_EDITOR_HEIGHT 120
#define MIN_BOTTOM_HEIGHT 90
#define NAVIGATION_HEIGHT 26
#define WORKSPACE_CAPTION_HEIGHT 14
#define LEFT_TABS_HEIGHT 22
#define STUDIO_MAX_DOCUMENT_STATES 256
#define STUDIO_MAX_NAVIGATION_HISTORY 256

typedef struct TreeTarget {
    char path[MAX_PATH];
    int line;
    int is_directory;
    int is_physical;
    TreeTargetKind kind;
} TreeTarget;

typedef struct WizardSymbol {
    SymbolDefinition definition;
    char path[MAX_PATH];
} WizardSymbol;

typedef struct StudioSourceLocation {
    char path[MAX_PATH];
    EditorViewState view;
} StudioSourceLocation;

typedef struct StudioWindow {
    HWND window;
    HWND toolbar;
    HWND toolbar2;
    HWND global_find;
    HWND global_find_button;
    HWND wizard_scope;
    HWND wizard_member;
    HWND wizard_action;
    HWND workspace_caption;
    HWND workspace_close;
    HWND tree;
    HWND left_tabs;
    HWND document_background;
    HWND editor;
    HWND tabs;
    HWND output;
    HWND callstack;
    HWND locals;
    HWND watch_list;
    HWND status;
    HMENU view_menu;
    HIMAGELIST toolbar_images;
    HIMAGELIST tree_images;
    HICON global_find_icon;
    HFONT classic_font;
    WNDPROC old_editor_proc;
    WNDPROC old_toolbar2_proc;
    WNDPROC old_global_find_proc;
    WNDPROC old_tabs_proc;
    WNDPROC old_output_proc;
    char log_path[MAX_PATH];
    char find_text[256];
    StudioProject project;
    DebugSession debug;
    int project_loaded;
    int paused;
    int debug_connected;
    int bottom_page;
    int left_page;
    int workspace_visible;
    int toolbar_visible;
    int wizardbar_visible;
    int output_window_visible;
    int status_visible;
    int stack_request;
    int locals_request;
    int left_width;
    int bottom_height;
    int splitter_drag;
    int splitter_drag_offset;
    SymbolFile navigation_files[256];
    WizardSymbol wizard_symbols[2048];
    int navigation_file_count;
    int wizard_symbol_count;
    int wizard_target_index;
    StudioSourceLocation document_states[STUDIO_MAX_DOCUMENT_STATES];
    int document_state_count;
    StudioSourceLocation navigation_history[STUDIO_MAX_NAVIGATION_HISTORY];
    int navigation_history_count;
    int navigation_history_index;
    struct {
        char expression[256];
        int request;
    } watches[64];
    int watch_count;
    struct {
        char path[MAX_PATH];
        int line;
    } breakpoints[512];
    int breakpoint_count;
} StudioWindow;

extern StudioWindow studio;

LRESULT CALLBACK studio_editor_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK studio_toolbar2_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK studio_global_find_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK studio_tabs_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK studio_output_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

/* Shell/UI services shared by the controller modules. */
void studio_set_document_close_button(int visible);
void studio_update_editor_title(const char *path);
void studio_refresh_editor_title(void);
void studio_show_error(HWND parent, const char *message);
void studio_output_append(const char *format, ...);
void studio_set_status(const char *text);
void studio_update_caret_status(void);
void studio_set_watch_values(const char *value);
void studio_list_add_row(HWND list, int row, const char *first, const char *second,
                         const char *third);
void studio_show_bottom_page(int page);
void studio_layout_controls(HWND window);
int studio_splitter_at(HWND window, int x, int y);
int studio_begin_splitter_drag(HWND window, int x, int y);
void studio_update_splitter_drag(HWND window, int x, int y);
void studio_paint_splitters(HWND window);
void studio_apply_system_window_colors(void);
HMENU studio_create_menu(void);
HWND studio_create_toolbar(HWND parent);
void studio_show_left_page(int page);
void studio_create_child_controls(HWND window);
void studio_set_view_check(int command, int checked);
void studio_open_bottom_page(int page);
HACCEL studio_create_accelerators(void);

/* Workspace, source navigation and editor commands. */
void studio_sync_current_breakpoints(void);
void studio_refresh_navigation_files(void);
void studio_refresh_navigation_symbols(void);
void studio_sync_wizard_to_caret(void);
int studio_go_to_navigation_symbol(void);
int studio_is_completion_character(int character);
TreeTarget *studio_selected_tree_target(void);
int studio_open_script(const char *path);
int studio_navigate_to(const char *path, int line);
void studio_navigate_back(void);
void studio_navigate_forward(void);
void studio_remember_current_document(void);
void studio_reset_document_navigation(void);
void studio_go_to_definition(void);
void studio_show_autocomplete(int forced);
void studio_populate_project_tree(void);
void studio_populate_class_tree(void);
int studio_load_project(const char *path);
void studio_close_workspace(void);
void studio_close_current_script(void);
void studio_new_project(void);
void studio_remove_selected_project_item(void);
void studio_rename_selected_project_item(void);
void studio_refresh_project_views(void);
void studio_new_script_file(void);
void studio_new_project_folder(void);
void studio_add_files_to_project(void);
void studio_save_script_as(void);
void studio_show_project_properties(void);
int studio_find_next_text(void);
void studio_find_text_dialog(void);
void studio_replace_text_dialog(void);
void studio_find_in_files_dialog(void);
void studio_run_global_find(void);
void studio_go_to_line_dialog(void);
void studio_insert_file_as_text(void);
void studio_open_project_dialog(void);
void studio_open_script_dialog(void);

/* Debugger presentation and commands. */
void studio_toggle_breakpoint(int line);
void studio_send_all_breakpoints(void);
void studio_start_or_continue(void);
void studio_stop_debugging(void);
void studio_step_command(const char *command);
void studio_evaluate_watches(void);
int studio_add_watch(const char *text);
void studio_add_editor_selection_to_watch(void);
void studio_delete_selected_watch(void);
void studio_process_debug_message(char *text);

#endif
