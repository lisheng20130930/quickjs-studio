#include "studio_internal.h"

#include "Scintilla.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

StudioWindow studio;

LRESULT CALLBACK studio_watch_edit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        studio_add_watch_expression();
        return 0;
    }
    return CallWindowProcA(studio.old_watch_edit_proc, window, message, wparam, lparam);
}

LRESULT CALLBACK studio_editor_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    enum {
        MENU_UNDO = 1,
        MENU_REDO,
        MENU_CUT,
        MENU_COPY,
        MENU_PASTE,
        MENU_DELETE,
        MENU_SELECT_ALL,
        MENU_GO_TO_DEFINITION,
        MENU_ADD_TO_WATCH
    };

    if (message == WM_CONTEXTMENU) {
        char selection[256];
        int has_selection = editor_selected_text(window, selection, sizeof(selection));
        char symbol[128];
        int has_symbol = editor_symbol_at_caret(window, symbol, sizeof(symbol));
        POINT point = {(LONG)(short)LOWORD(lparam), (LONG)(short)HIWORD(lparam)};
        HMENU menu = CreatePopupMenu();
        int command;

        if (point.x == -1 && point.y == -1)
            GetCursorPos(&point);
        AppendMenuA(menu, MF_STRING, MENU_UNDO, "Undo");
        AppendMenuA(menu, MF_STRING, MENU_REDO, "Redo");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, MENU_CUT, "Cut");
        AppendMenuA(menu, MF_STRING, MENU_COPY, "Copy");
        AppendMenuA(menu, MF_STRING, MENU_PASTE, "Paste");
        AppendMenuA(menu, MF_STRING, MENU_DELETE, "Delete");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, MENU_SELECT_ALL, "Select All");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, MENU_GO_TO_DEFINITION, "Go to Definition");
        AppendMenuA(menu, MF_STRING, MENU_ADD_TO_WATCH, "Add to Watch");
        EnableMenuItem(menu, MENU_UNDO,
                       MF_BYCOMMAND |
                           (SendMessageA(window, SCI_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_REDO,
                       MF_BYCOMMAND |
                           (SendMessageA(window, SCI_CANREDO, 0, 0) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_CUT, MF_BYCOMMAND | (has_selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_COPY, MF_BYCOMMAND | (has_selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_DELETE, MF_BYCOMMAND | (has_selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_PASTE,
                       MF_BYCOMMAND |
                           (SendMessageA(window, SCI_CANPASTE, 0, 0) ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_ADD_TO_WATCH,
                       MF_BYCOMMAND | (has_selection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, MENU_GO_TO_DEFINITION,
                       MF_BYCOMMAND | (has_symbol ? MF_ENABLED : MF_GRAYED));
        command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0,
                                 studio.window, NULL);
        DestroyMenu(menu);
        switch (command) {
        case MENU_UNDO:
            SendMessageA(window, SCI_UNDO, 0, 0);
            break;
        case MENU_REDO:
            SendMessageA(window, SCI_REDO, 0, 0);
            break;
        case MENU_CUT:
            SendMessageA(window, SCI_CUT, 0, 0);
            break;
        case MENU_COPY:
            SendMessageA(window, SCI_COPY, 0, 0);
            break;
        case MENU_PASTE:
            SendMessageA(window, SCI_PASTE, 0, 0);
            break;
        case MENU_DELETE:
            SendMessageA(window, SCI_CLEAR, 0, 0);
            break;
        case MENU_SELECT_ALL:
            SendMessageA(window, SCI_SELECTALL, 0, 0);
            break;
        case MENU_GO_TO_DEFINITION:
            studio_go_to_definition();
            break;
        case MENU_ADD_TO_WATCH:
            studio_add_editor_selection_to_watch();
            break;
        }
        return 0;
    }
    return CallWindowProcA(studio.old_editor_proc, window, message, wparam, lparam);
}

LRESULT CALLBACK studio_output_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_LBUTTONDBLCLK) {
        POINT point = {(LONG)(short)LOWORD(lparam), (LONG)(short)HIWORD(lparam)};
        LRESULT character = SendMessageA(window, EM_CHARFROMPOS, 0, MAKELPARAM(point.x, point.y));
        int line = (int)SendMessageA(window, EM_LINEFROMCHAR, LOWORD(character), 0);
        int start = (int)SendMessageA(window, EM_LINEINDEX, line, 0);
        int length = (int)SendMessageA(window, EM_LINELENGTH, start, 0);
        char text[4096];
        char *line_marker;
        int source_line;

        if (length > 0 && length < (int)sizeof(text) - 1) {
            *(WORD *)text = (WORD)length;
            length = (int)SendMessageA(window, EM_GETLINE, line, (LPARAM)text);
            text[length] = '\0';
            line_marker = strrchr(text, '(');
            if (line_marker && sscanf(line_marker, "(%d):", &source_line) == 1) {
                *line_marker = '\0';
                if (GetFileAttributesA(text) != INVALID_FILE_ATTRIBUTES)
                    studio_navigate_to(text, source_line);
            }
        }
    }
    return CallWindowProcA(studio.old_output_proc, window, message, wparam, lparam);
}

static LRESULT handle_command(HWND window, int command)
{
    switch (command) {
    case ID_FILE_NEW_PROJECT:
        studio_new_project();
        break;
    case ID_FILE_OPEN_PROJECT:
        studio_open_project_dialog();
        break;
    case ID_FILE_NEW_SCRIPT:
        studio_new_script_file();
        break;
    case ID_FILE_OPEN_SCRIPT:
        studio_open_script_dialog();
        break;
    case ID_FILE_SAVE:
        if (!editor_path()[0]) {
            studio_save_script_as();
        } else if (!editor_save(studio.editor)) {
            studio_show_error(window, "Cannot save the current script.");
        } else {
            studio_refresh_navigation_symbols();
            studio_refresh_editor_title();
        }
        break;
    case ID_FILE_SAVE_AS:
        studio_save_script_as();
        break;
    case ID_FILE_CLOSE_WORKSPACE:
        studio_close_workspace();
        break;
    case ID_FILE_EXIT:
        DestroyWindow(window);
        break;
    case ID_DOCUMENT_CLOSE:
        studio_close_current_script();
        break;
    case ID_EDIT_UNDO:
        SendMessageA(studio.editor, SCI_UNDO, 0, 0);
        break;
    case ID_EDIT_REDO:
        SendMessageA(studio.editor, SCI_REDO, 0, 0);
        break;
    case ID_EDIT_CUT:
        SendMessageA(studio.editor, SCI_CUT, 0, 0);
        break;
    case ID_EDIT_COPY:
        SendMessageA(studio.editor, SCI_COPY, 0, 0);
        break;
    case ID_EDIT_PASTE:
        SendMessageA(studio.editor, SCI_PASTE, 0, 0);
        break;
    case ID_EDIT_DELETE:
        SendMessageA(studio.editor, SCI_CLEAR, 0, 0);
        break;
    case ID_EDIT_SELECT_ALL:
        SendMessageA(studio.editor, SCI_SELECTALL, 0, 0);
        break;
    case ID_EDIT_FIND:
        studio_find_text_dialog();
        break;
    case ID_EDIT_FIND_NEXT:
        if (!studio.find_text[0])
            studio_find_text_dialog();
        else
            studio_find_next_text();
        break;
    case ID_EDIT_FIND_IN_FILES:
        studio_find_in_files_dialog();
        break;
    case ID_EDIT_REPLACE:
        studio_replace_text_dialog();
        break;
    case ID_EDIT_GO_TO_LINE:
        studio_go_to_line_dialog();
        break;
    case ID_EDIT_GO_TO_DEFINITION:
        studio_go_to_definition();
        break;
    case ID_EDIT_NAVIGATE_BACK:
        studio_navigate_back();
        break;
    case ID_EDIT_NAVIGATE_FORWARD:
        studio_navigate_forward();
        break;
    case ID_EDIT_AUTOCOMPLETE:
        studio_show_autocomplete(1);
        break;
    case ID_EDIT_TOGGLE_BOOKMARK:
        editor_toggle_bookmark(studio.editor);
        break;
    case ID_EDIT_NEXT_BOOKMARK:
        if (GetFocus() == studio.tree && studio.left_page == PAGE_FILE_VIEW)
            studio_rename_selected_project_item();
        else if (!editor_next_bookmark(studio.editor, 1))
            studio_set_status("No bookmarks");
        break;
    case ID_EDIT_PREVIOUS_BOOKMARK:
        if (!editor_next_bookmark(studio.editor, 0))
            studio_set_status("No bookmarks");
        break;
    case ID_EDIT_CLEAR_BOOKMARKS:
        editor_clear_bookmarks(studio.editor);
        break;
    case ID_INSERT_FILE_AS_TEXT:
        studio_insert_file_as_text();
        break;
    case ID_DEBUG_START:
        studio_start_or_continue();
        break;
    case ID_DEBUG_STOP:
        studio_stop_debugging();
        break;
    case ID_DEBUG_BREAKPOINT:
        studio_toggle_breakpoint(editor_current_line(studio.editor));
        break;
    case ID_DEBUG_STEP_OVER:
        studio_step_command("next");
        break;
    case ID_DEBUG_STEP_INTO:
        studio_step_command("stepIn");
        break;
    case ID_DEBUG_STEP_OUT:
        studio_step_command("stepOut");
        break;
    case ID_VIEW_OUTPUT:
        studio_open_bottom_page(PAGE_OUTPUT);
        break;
    case ID_VIEW_CALLSTACK:
        studio_open_bottom_page(PAGE_CALLSTACK);
        break;
    case ID_VIEW_LOCALS:
        studio_open_bottom_page(PAGE_LOCALS);
        break;
    case ID_VIEW_WATCH:
        studio_open_bottom_page(PAGE_WATCH);
        SetFocus(studio.watch_edit);
        break;
    case ID_VIEW_WORKSPACE:
        studio.workspace_visible = !studio.workspace_visible;
        studio_set_view_check(ID_VIEW_WORKSPACE, studio.workspace_visible);
        studio_layout_controls(window);
        break;
    case ID_VIEW_TOOLBAR:
        studio.toolbar_visible = !studio.toolbar_visible;
        studio_set_view_check(ID_VIEW_TOOLBAR, studio.toolbar_visible);
        studio_layout_controls(window);
        break;
    case ID_VIEW_WIZARDBAR:
        studio.wizardbar_visible = !studio.wizardbar_visible;
        studio_set_view_check(ID_VIEW_WIZARDBAR, studio.wizardbar_visible);
        studio_layout_controls(window);
        break;
    case ID_VIEW_OUTPUT_WINDOW:
        studio.output_window_visible = !studio.output_window_visible;
        studio_set_view_check(ID_VIEW_OUTPUT_WINDOW, studio.output_window_visible);
        studio_show_bottom_page(studio.bottom_page);
        studio_layout_controls(window);
        break;
    case ID_VIEW_STATUS_BAR:
        studio.status_visible = !studio.status_visible;
        studio_set_view_check(ID_VIEW_STATUS_BAR, studio.status_visible);
        studio_layout_controls(window);
        break;
    case ID_PROJECT_NEW_FOLDER:
        studio_new_project_folder();
        break;
    case ID_PROJECT_ADD_FILES:
        studio_add_files_to_project();
        break;
    case ID_PROJECT_RENAME:
        studio_rename_selected_project_item();
        break;
    case ID_PROJECT_REMOVE:
        studio_remove_selected_project_item();
        break;
    case ID_PROJECT_REFRESH:
        if (studio.project_loaded)
            studio_refresh_project_views();
        break;
    case ID_PROJECT_PROPERTIES:
        studio_show_project_properties();
        break;
    case ID_WATCH_ADD:
        studio_add_watch_expression();
        break;
    case ID_WIZARD_ACTION:
        studio_go_to_definition();
        break;
    case ID_WORKSPACE_CLOSE:
        studio.workspace_visible = 0;
        studio_set_view_check(ID_VIEW_WORKSPACE, 0);
        studio_layout_controls(window);
        break;
    case ID_TOOLS_ADD_WATCH:
        studio_add_editor_selection_to_watch();
        break;
    case ID_HELP_ABOUT:
        MessageBoxA(window,
                    "QuickJS Studio\r\n\r\nA lightweight QuickJS debugger with the classic "
                    "Visual C++ 6.0 workspace.",
                    "About QuickJS Studio", MB_OK | MB_ICONINFORMATION);
        break;
    }
    return 0;
}

static void open_selected_tree_item(void)
{
    TreeTarget *target = studio_selected_tree_target();

    if (!target || target->is_directory)
        return;
    if (target->line > 0)
        studio_navigate_to(target->path, target->line);
    else
        studio_open_script(target->path);
}

static void show_tree_context_menu(void)
{
    POINT screen;
    POINT client;
    TVHITTESTINFO hit = {0};
    HMENU menu;
    int command;
    TreeTarget *target;

    GetCursorPos(&screen);
    client = screen;
    ScreenToClient(studio.tree, &client);
    hit.pt = client;
    TreeView_HitTest(studio.tree, &hit);
    if (hit.hItem)
        TreeView_SelectItem(studio.tree, hit.hItem);
    target = studio_selected_tree_target();
    menu = CreatePopupMenu();
    if (studio.left_page == PAGE_FILE_VIEW) {
        AppendMenuA(menu, MF_STRING, ID_PROJECT_NEW_FOLDER, "New Folder...");
        AppendMenuA(menu, MF_STRING, ID_PROJECT_ADD_FILES, "Add Files to Folder...");
        AppendMenuA(menu, MF_STRING, ID_FILE_NEW_SCRIPT, "New JavaScript File...");
        if (target && target->is_physical) {
            AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(menu, MF_STRING, ID_PROJECT_RENAME, "Rename");
            AppendMenuA(menu, MF_STRING, ID_PROJECT_REMOVE, "Remove From Project");
        }
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, ID_PROJECT_REFRESH, "Refresh");
        AppendMenuA(menu, MF_STRING, ID_PROJECT_PROPERTIES, "Settings...");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING | MF_CHECKED | MF_GRAYED, 0, "Docking View");
        AppendMenuA(menu, MF_STRING, ID_VIEW_WORKSPACE, "Hide");
        AppendMenuA(menu, MF_STRING, ID_PROJECT_PROPERTIES, "Properties");
    } else {
        AppendMenuA(menu, MF_STRING, ID_WIZARD_ACTION, "Go To Definition");
        AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(menu, MF_STRING, ID_VIEW_WORKSPACE, "Hide");
        AppendMenuA(menu, MF_STRING, ID_PROJECT_PROPERTIES, "Properties");
    }
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0,
                             studio.window, NULL);
    DestroyMenu(menu);
    if (command == ID_WIZARD_ACTION)
        open_selected_tree_item();
    else if (command)
        handle_command(studio.window, command);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        studio.window = window;
        studio_reset_document_navigation();
        studio_create_child_controls(window);
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED)
            studio_layout_controls(window);
        return 0;
    case WM_PAINT:
        studio_paint_splitters(window);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point;
            int splitter;
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            splitter = studio.splitter_drag ? studio.splitter_drag
                                            : studio_splitter_at(window, point.x, point.y);
            if (splitter == SPLITTER_VERTICAL) {
                SetCursor(LoadCursorA(NULL, IDC_SIZEWE));
                return TRUE;
            }
            if (splitter == SPLITTER_HORIZONTAL) {
                SetCursor(LoadCursorA(NULL, IDC_SIZENS));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        if (!studio_begin_splitter_drag(window, x, y))
            break;
        SetCapture(window);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (studio.splitter_drag != SPLITTER_NONE) {
            int x = (short)LOWORD(lparam);
            int y = (short)HIWORD(lparam);
            studio_update_splitter_drag(window, x, y);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (studio.splitter_drag != SPLITTER_NONE) {
            studio.splitter_drag = SPLITTER_NONE;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        studio.splitter_drag = SPLITTER_NONE;
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = 640;
        limits->ptMinTrackSize.y = 480;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_WIZARD_SCOPE && HIWORD(wparam) == CBN_SELCHANGE) {
            int selected = (int)SendMessageA(studio.wizard_scope, CB_GETCURSEL, 0, 0);
            if (selected >= 0) {
                studio.wizard_target_index =
                    (int)SendMessageA(studio.wizard_scope, CB_GETITEMDATA, (WPARAM)selected, 0);
                studio_go_to_navigation_symbol();
            }
            return 0;
        }
        if (LOWORD(wparam) == ID_WIZARD_MEMBER && HIWORD(wparam) == CBN_SELCHANGE) {
            int selected = (int)SendMessageA(studio.wizard_member, CB_GETCURSEL, 0, 0);
            if (selected >= 0)
                studio.wizard_target_index =
                    (int)SendMessageA(studio.wizard_member, CB_GETITEMDATA, (WPARAM)selected, 0);
            return 0;
        }
        return handle_command(window, LOWORD(wparam));
    case WM_CTLCOLOREDIT:
        if ((HWND)lparam == studio.watch_edit) {
            SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor((HDC)wparam, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lparam == studio.document_background) {
            SetBkColor((HDC)wparam, GetSysColor(COLOR_APPWORKSPACE));
            return (LRESULT)GetSysColorBrush(COLOR_APPWORKSPACE);
        }
        if ((HWND)lparam == studio.output) {
            SetTextColor((HDC)wparam, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor((HDC)wparam, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    case WM_SYSCOLORCHANGE:
        studio_apply_system_window_colors();
        break;
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom == ID_BOTTOM_TABS && header->code == TCN_SELCHANGE)
            studio_show_bottom_page(TabCtrl_GetCurSel(studio.tabs));
        else if (header->idFrom == ID_LEFT_TABS && header->code == TCN_SELCHANGE)
            studio_show_left_page(TabCtrl_GetCurSel(studio.left_tabs));
        else if (header->idFrom == ID_EDITOR && header->code == SCN_MARGINCLICK) {
            SCNotification *notification = (SCNotification *)lparam;
            int line =
                (int)SendMessageA(studio.editor, SCI_LINEFROMPOSITION, notification->position, 0);
            studio_toggle_breakpoint(line);
        } else if (header->idFrom == ID_EDITOR && header->code == SCN_MODIFIED) {
            SCNotification *notification = (SCNotification *)lparam;
            editor_update_line_number_margin(studio.editor);
            if (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
                editor_apply_semantic_colors(studio.editor);
                studio_refresh_editor_title();
            }
        } else if (header->idFrom == ID_EDITOR &&
                   (header->code == SCN_SAVEPOINTLEFT || header->code == SCN_SAVEPOINTREACHED)) {
            studio_refresh_editor_title();
        } else if (header->idFrom == ID_EDITOR && header->code == SCN_CHARADDED) {
            SCNotification *notification = (SCNotification *)lparam;
            if (notification->ch == '\n')
                editor_handle_newline(studio.editor);
            else if (notification->ch == '}')
                editor_handle_closing_brace(studio.editor);
            else if (notification->ch == '.')
                studio_show_autocomplete(1);
            else if (studio_is_completion_character(notification->ch))
                studio_show_autocomplete(0);
        } else if (header->idFrom == ID_EDITOR && header->code == SCN_UPDATEUI) {
            editor_update_brace_highlight(studio.editor);
            studio_sync_wizard_to_caret();
            studio_update_caret_status();
        } else if (header->idFrom == ID_TREE && header->code == NM_RCLICK) {
            show_tree_context_menu();
        } else if (header->idFrom == ID_TREE && header->code == NM_DBLCLK) {
            open_selected_tree_item();
        } else if (header->idFrom == ID_TREE && header->code == TVN_KEYDOWN) {
            NMTVKEYDOWN *key = (NMTVKEYDOWN *)lparam;
            if (studio.left_page == PAGE_FILE_VIEW && key->wVKey == VK_DELETE)
                studio_remove_selected_project_item();
            else if (studio.left_page == PAGE_FILE_VIEW && key->wVKey == VK_F2)
                studio_rename_selected_project_item();
        } else if (header->idFrom == ID_TREE && header->code == TVN_DELETEITEMA) {
            NMTREEVIEWA *tree = (NMTREEVIEWA *)lparam;
            if (tree->itemOld.lParam)
                HeapFree(GetProcessHeap(), 0, (void *)tree->itemOld.lParam);
        } else if (header->idFrom == ID_WATCH_LIST && header->code == LVN_KEYDOWN) {
            NMLVKEYDOWN *key = (NMLVKEYDOWN *)lparam;
            if (key->wVKey == VK_DELETE)
                studio_delete_selected_watch();
        }
        return 0;
    }
    case WM_QJS_DEBUG_CONNECTED:
        KillTimer(window, 1);
        studio.debug_connected = 1;
        studio_output_append("Debugger connected.\r\n");
        studio_send_all_breakpoints();
        studio_set_status("Debugger connected");
        return 0;
    case WM_QJS_DEBUG_MESSAGE:
        studio_process_debug_message((char *)lparam);
        return 0;
    case WM_QJS_DEBUG_TERMINATED:
        KillTimer(window, 1);
        studio.debug_connected = 0;
        studio.paused = 0;
        editor_clear_execution_line(studio.editor);
        studio_set_watch_values("<not available>");
        studio_set_status("Process exited");
        studio_output_append("Process exited.\r\n");
        return 0;
    case WM_TIMER:
        if (wparam == 1 && !studio.debug_connected) {
            KillTimer(window, 1);
            studio_output_append(
                "Debugger connection timed out. Check that the target includes the "
                "QuickJS Debug Agent.\r\n");
            studio_stop_debugging();
        }
        return 0;
    case WM_QJS_DEBUG_OUTPUT:
        studio_output_append("%s", (const char *)lparam);
        debug_session_free_message((char *)lparam);
        return 0;
    case WM_QJS_DEBUG_TRACE:
        studio_output_append("%s\r\n", (const char *)lparam);
        debug_session_free_message((char *)lparam);
        return 0;
    case WM_CLOSE:
        if (editor_is_modified(studio.editor)) {
            int result = MessageBoxA(window, "Save the current script?", "QuickJS Studio",
                                     MB_YESNOCANCEL | MB_ICONQUESTION);
            if (result == IDCANCEL)
                return 0;
            if (result == IDYES && !editor_save(studio.editor))
                return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        debug_session_stop(&studio.debug);
        editor_unload_components();
        if (studio.toolbar_images) {
            SendMessageA(studio.toolbar, TB_SETIMAGELIST, 0, 0);
            ImageList_Destroy(studio.toolbar_images);
            studio.toolbar_images = NULL;
        }
        if (studio.tree_images) {
            TreeView_SetImageList(studio.tree, NULL, TVSIL_NORMAL);
            TabCtrl_SetImageList(studio.left_tabs, NULL);
            ImageList_Destroy(studio.tree_images);
            studio.tree_images = NULL;
        }
        studio.classic_font = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static void default_project_path(char *path, size_t capacity)
{
    char executable[MAX_PATH];
    char *slash;
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    slash = strrchr(executable, '\\');
    if (slash)
        *slash = '\0';
    snprintf(path, capacity, "%s\\..\\..\\..\\examples\\quickjs-studio.qsp", executable);
}

static void initialize_log_path(void)
{
    char *slash;
    FILE *log;
    GetModuleFileNameA(NULL, studio.log_path, sizeof(studio.log_path));
    slash = strrchr(studio.log_path, '\\');
    if (slash)
        snprintf(slash + 1, (size_t)(studio.log_path + sizeof(studio.log_path) - slash - 1),
                 "quickjs-studio.log");
    log = fopen(studio.log_path, "wb");
    if (log)
        fclose(log);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    WNDCLASSEXA window_class = {0};
    MSG message;
    HACCEL accelerators;
    char project_path[MAX_PATH];
    HWND window;
    (void)previous;
    (void)show;

    memset(&studio, 0, sizeof(studio));
    studio.left_width = 210;
    studio.bottom_height = 190;
    studio.workspace_visible = 1;
    studio.toolbar_visible = 1;
    studio.wizardbar_visible = 1;
    studio.output_window_visible = 1;
    studio.status_visible = 1;
    initialize_log_path();
    if (!InitCommonControlsEx(&controls) || !editor_load_components()) {
        studio_show_error(NULL, "Scintilla.dll or lexilla.dll is missing.");
        return 1;
    }
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    window_class.lpszClassName = "QuickJSStudioWindow";
    window_class.hIconSm = window_class.hIcon;
    if (!RegisterClassExA(&window_class))
        return 1;
    window = CreateWindowExA(0, window_class.lpszClassName, "QuickJS Studio",
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                             1100, 760, NULL, studio_create_menu(), instance, NULL);
    if (!window)
        return 1;
    accelerators = studio_create_accelerators();
    ShowWindow(window, SW_SHOWMAXIMIZED);
    UpdateWindow(window);

    if (command_line && command_line[0]) {
        snprintf(project_path, sizeof(project_path), "%s", command_line);
        if (project_path[0] == '"') {
            size_t length = strlen(project_path);
            memmove(project_path, project_path + 1, length);
            length = strlen(project_path);
            if (length && project_path[length - 1] == '"')
                project_path[length - 1] = '\0';
        }
    } else {
        default_project_path(project_path, sizeof(project_path));
    }
    studio_load_project(project_path);

    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorA(window, accelerators, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    DestroyAcceleratorTable(accelerators);
    return (int)message.wParam;
}
