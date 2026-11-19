#include "studio_internal.h"

#include "Scintilla.h"
#include "editor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void studio_set_document_close_button(int visible)
{
    HMENU menu;
    MENUITEMINFOA item = {0};
    MENUITEMINFOA probe = {0};
    int present;

    if (!studio.window)
        return;
    menu = GetMenu(studio.window);
    if (!menu)
        return;
    probe.cbSize = sizeof(probe);
    probe.fMask = MIIM_ID;
    present = GetMenuItemInfoA(menu, ID_DOCUMENT_CLOSE, FALSE, &probe) != FALSE;
    if (visible && !present) {
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_BITMAP;
        item.fType = MFT_RIGHTJUSTIFY;
        item.wID = ID_DOCUMENT_CLOSE;
        item.hbmpItem = HBMMENU_MBAR_CLOSE;
        InsertMenuItemA(menu, GetMenuItemCount(menu), TRUE, &item);
    } else if (!visible && present) {
        RemoveMenu(menu, ID_DOCUMENT_CLOSE, MF_BYCOMMAND);
    }
    DrawMenuBar(studio.window);
}

static void set_editor_title(const char *path)
{
    char title[MAX_PATH + 80];

    if (!path || !path[0])
        SetWindowTextA(studio.window, "QuickJS Studio");
    else {
        snprintf(title, sizeof(title), "%s%s - QuickJS Studio", path,
                 editor_is_modified(studio.editor) ? " *" : "");
        SetWindowTextA(studio.window, title);
    }
}

void studio_update_editor_title(const char *path)
{
    set_editor_title(path);
    if (!path || !path[0]) {
        studio_set_status("Ready");
        return;
    }
    studio_set_status(path);
}

void studio_refresh_editor_title(void)
{
    set_editor_title(editor_path());
}

void studio_show_error(HWND parent, const char *message)
{
    MessageBoxA(parent, message, "QuickJS Studio", MB_OK | MB_ICONERROR);
}

static HFONT create_fixedsys_font(void)
{
    return (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
}

static void set_control_font(HWND control, HFONT font)
{
    if (control && font)
        SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
}

void studio_apply_system_window_colors(void)
{
    COLORREF background = GetSysColor(COLOR_WINDOW);
    COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
    HWND lists[] = {studio.callstack, studio.locals, studio.watch_list};

    TreeView_SetBkColor(studio.tree, background);
    TreeView_SetTextColor(studio.tree, text);
    for (int index = 0; index < (int)ARRAYSIZE(lists); index++) {
        ListView_SetBkColor(lists[index], background);
        ListView_SetTextBkColor(lists[index], background);
        ListView_SetTextColor(lists[index], text);
    }
    InvalidateRect(studio.tree, NULL, TRUE);
    InvalidateRect(studio.document_background, NULL, TRUE);
    InvalidateRect(studio.output, NULL, TRUE);
    InvalidateRect(studio.callstack, NULL, TRUE);
    InvalidateRect(studio.locals, NULL, TRUE);
    InvalidateRect(studio.watch_edit, NULL, TRUE);
    InvalidateRect(studio.watch_list, NULL, TRUE);
}

void studio_output_append(const char *format, ...)
{
    char text[2048];
    int length;
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (studio.log_path[0]) {
        FILE *log = fopen(studio.log_path, "ab");
        if (log) {
            fwrite(text, 1, strlen(text), log);
            fclose(log);
        }
    }
    length = GetWindowTextLengthA(studio.output);
    SendMessageA(studio.output, EM_SETSEL, length, length);
    SendMessageA(studio.output, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void studio_set_status(const char *text)
{
    SendMessageA(studio.status, SB_SETTEXTA, 0, (LPARAM)text);
}

void studio_update_caret_status(void)
{
    char line_text[32];
    char column_text[32];
    LRESULT position;

    if (!studio.status || !studio.editor)
        return;
    position = SendMessageA(studio.editor, SCI_GETCURRENTPOS, 0, 0);
    snprintf(line_text, sizeof(line_text), "Ln %d", editor_current_line(studio.editor) + 1);
    snprintf(column_text, sizeof(column_text), "Col %d",
             (int)SendMessageA(studio.editor, SCI_GETCOLUMN, position, 0) + 1);
    SendMessageA(studio.status, SB_SETTEXTA, 1, (LPARAM)line_text);
    SendMessageA(studio.status, SB_SETTEXTA, 2, (LPARAM)column_text);
    SendMessageA(studio.status, SB_SETTEXTA, 3, (LPARAM) "INS");
}

static void layout_status_bar(void)
{
    RECT status;
    int parts[4];

    GetClientRect(studio.status, &status);
    parts[0] = status.right - 180;
    parts[1] = status.right - 120;
    parts[2] = status.right - 60;
    parts[3] = -1;
    SendMessageA(studio.status, SB_SETPARTS, ARRAYSIZE(parts), (LPARAM)parts);
    studio_update_caret_status();
}

void studio_set_watch_values(const char *value)
{
    for (int row = 0; row < studio.watch_count; row++) {
        studio.watches[row].request = 0;
        ListView_SetItemText(studio.watch_list, row, 1, (char *)value);
        ListView_SetItemText(studio.watch_list, row, 2, "");
    }
}

static void list_add_column(HWND list, int column, int width, const char *title)
{
    LVCOLUMNA item = {0};
    item.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    item.pszText = (char *)title;
    item.cx = width;
    item.iSubItem = column;
    ListView_InsertColumn(list, column, &item);
}

void studio_list_add_row(HWND list, int row, const char *first, const char *second,
                         const char *third)
{
    LVITEMA item = {0};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.pszText = (char *)(first ? first : "");
    ListView_InsertItem(list, &item);
    if (second)
        ListView_SetItemText(list, row, 1, (char *)second);
    if (third)
        ListView_SetItemText(list, row, 2, (char *)third);
}

void studio_show_bottom_page(int page)
{
    int visible = studio.output_window_visible;

    studio.bottom_page = page;
    TabCtrl_SetCurSel(studio.tabs, page);
    ShowWindow(studio.tabs, visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.output, visible && page == PAGE_OUTPUT ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.callstack, visible && page == PAGE_CALLSTACK ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.locals, visible && page == PAGE_LOCALS ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.watch_edit, visible && page == PAGE_WATCH ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.watch_add, visible && page == PAGE_WATCH ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.watch_list, visible && page == PAGE_WATCH ? SW_SHOW : SW_HIDE);
}

typedef struct LayoutMetrics {
    int navigation_y;
    int top;
    int bottom;
    int width;
    int height;
    int horizontal_y;
} LayoutMetrics;

static LayoutMetrics get_layout_metrics(HWND window)
{
    LayoutMetrics metrics;
    RECT client;
    RECT toolbar_rect;
    RECT status_rect;
    int navigation_visible = studio.wizardbar_visible && editor_path()[0];

    GetClientRect(window, &client);
    if (studio.toolbar_visible)
        GetWindowRect(studio.toolbar, &toolbar_rect);
    else
        SetRectEmpty(&toolbar_rect);
    if (studio.status_visible)
        GetWindowRect(studio.status, &status_rect);
    else
        SetRectEmpty(&status_rect);
    metrics.navigation_y = studio.toolbar_visible ? toolbar_rect.bottom - toolbar_rect.top : 0;
    metrics.top = metrics.navigation_y + (navigation_visible ? NAVIGATION_HEIGHT : 0);
    metrics.bottom = studio.status_visible ? status_rect.bottom - status_rect.top : 0;
    metrics.width = client.right;
    metrics.height = client.bottom - metrics.top - metrics.bottom;
    metrics.horizontal_y = metrics.top + metrics.height -
                           (studio.output_window_visible ? studio.bottom_height : 0) -
                           (studio.output_window_visible ? SPLITTER_SIZE : 0);
    return metrics;
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (maximum < minimum)
        maximum = minimum;
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

void studio_layout_controls(HWND window)
{
    LayoutMetrics metrics;
    int tabs_height = 25;
    int wizard_x;
    int wizard_width;
    int context_width;
    int definition_width;
    int output_content_height;
    int output_content_y;
    int workspace_width;
    int vertical_splitter;
    int output_height;
    int horizontal_splitter;
    int navigation_visible = studio.wizardbar_visible && editor_path()[0];
    int workspace_top;
    int workspace_height;
    const int action_width = 38;
    const int gap = 3;

    if (studio.toolbar_visible)
        SendMessageA(studio.toolbar, TB_AUTOSIZE, 0, 0);
    ShowWindow(studio.toolbar, studio.toolbar_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.status, studio.status_visible ? SW_SHOW : SW_HIDE);
    if (studio.status_visible) {
        SendMessageA(studio.status, WM_SIZE, 0, 0);
        layout_status_bar();
    }
    metrics = get_layout_metrics(window);
    workspace_top = metrics.navigation_y;
    workspace_height = metrics.top + metrics.height - workspace_top;
    studio.left_width = clamp_int(studio.left_width, MIN_TREE_WIDTH,
                                  metrics.width - MIN_EDITOR_WIDTH - SPLITTER_SIZE);
    studio.bottom_height = clamp_int(studio.bottom_height, MIN_BOTTOM_HEIGHT,
                                     metrics.height - MIN_EDITOR_HEIGHT - SPLITTER_SIZE);
    workspace_width = studio.workspace_visible ? studio.left_width : 0;
    vertical_splitter = studio.workspace_visible ? SPLITTER_SIZE : 0;
    output_height = studio.output_window_visible ? studio.bottom_height : 0;
    output_content_height = output_height > tabs_height + 5 ? output_height - tabs_height - 5 : 0;
    horizontal_splitter = studio.output_window_visible ? SPLITTER_SIZE : 0;
    metrics.horizontal_y = metrics.top + metrics.height - output_height - horizontal_splitter;
    output_content_y = metrics.horizontal_y + horizontal_splitter + 4;

    wizard_x = workspace_width + vertical_splitter;
    wizard_width = metrics.width - wizard_x;
    context_width = wizard_width * 30 / 100;
    definition_width = wizard_width - context_width - action_width - gap * 2;
    MoveWindow(studio.wizard_scope, wizard_x, metrics.navigation_y + 2, context_width,
               NAVIGATION_HEIGHT - 3, TRUE);
    wizard_x += context_width + gap;
    MoveWindow(studio.wizard_member, wizard_x, metrics.navigation_y + 2, definition_width,
               NAVIGATION_HEIGHT - 3, TRUE);
    wizard_x += definition_width + gap;
    MoveWindow(studio.wizard_action, wizard_x, metrics.navigation_y + 2, action_width,
               NAVIGATION_HEIGHT - 3, TRUE);
    ShowWindow(studio.wizard_scope, navigation_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.wizard_member, navigation_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.wizard_action, navigation_visible ? SW_SHOW : SW_HIDE);

    MoveWindow(studio.workspace_caption, 1, workspace_top,
               workspace_width > 15 ? workspace_width - 15 : 0, WORKSPACE_CAPTION_HEIGHT, TRUE);
    MoveWindow(studio.workspace_close, workspace_width > 14 ? workspace_width - 14 : 0,
               workspace_top, 13, 13, TRUE);
    MoveWindow(studio.tree, 0, workspace_top + WORKSPACE_CAPTION_HEIGHT, workspace_width,
               workspace_height - WORKSPACE_CAPTION_HEIGHT - LEFT_TABS_HEIGHT, TRUE);
    MoveWindow(studio.left_tabs, 0, workspace_top + workspace_height - LEFT_TABS_HEIGHT,
               workspace_width, LEFT_TABS_HEIGHT, TRUE);
    ShowWindow(studio.tree, studio.workspace_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.left_tabs, studio.workspace_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.workspace_caption, studio.workspace_visible ? SW_SHOW : SW_HIDE);
    ShowWindow(studio.workspace_close, studio.workspace_visible ? SW_SHOW : SW_HIDE);
    MoveWindow(studio.editor, workspace_width + vertical_splitter, metrics.top,
               metrics.width - workspace_width - vertical_splitter,
               metrics.height - output_height - horizontal_splitter, TRUE);
    MoveWindow(studio.document_background, workspace_width + vertical_splitter, metrics.top,
               metrics.width - workspace_width - vertical_splitter,
               metrics.height - output_height - horizontal_splitter, TRUE);
    MoveWindow(studio.tabs, workspace_width + vertical_splitter,
               metrics.horizontal_y + horizontal_splitter,
               metrics.width - workspace_width - vertical_splitter, output_height, TRUE);
    MoveWindow(studio.output, workspace_width + vertical_splitter + 5, output_content_y,
               metrics.width - workspace_width - vertical_splitter - 10, output_content_height,
               TRUE);
    MoveWindow(studio.callstack, workspace_width + vertical_splitter + 5, output_content_y,
               metrics.width - workspace_width - vertical_splitter - 10, output_content_height,
               TRUE);
    MoveWindow(studio.locals, workspace_width + vertical_splitter + 5, output_content_y,
               metrics.width - workspace_width - vertical_splitter - 10, output_content_height,
               TRUE);
    MoveWindow(studio.watch_edit, workspace_width + vertical_splitter + 5, output_content_y,
               metrics.width - workspace_width - vertical_splitter - 67, 23, TRUE);
    MoveWindow(studio.watch_add, metrics.width - 58, output_content_y, 53, 23, TRUE);
    MoveWindow(studio.watch_list, workspace_width + vertical_splitter + 5, output_content_y + 25,
               metrics.width - workspace_width - vertical_splitter - 10,
               output_height > tabs_height + 30 ? output_height - tabs_height - 30 : 0, TRUE);
    studio_show_bottom_page(studio.bottom_page);
    InvalidateRect(window, NULL, TRUE);
}

int studio_splitter_at(HWND window, int x, int y)
{
    LayoutMetrics metrics = get_layout_metrics(window);
    metrics.horizontal_y = metrics.top + metrics.height - studio.bottom_height - SPLITTER_SIZE;
    if (studio.workspace_visible && x >= studio.left_width &&
        x < studio.left_width + SPLITTER_SIZE && y >= metrics.navigation_y &&
        y < metrics.top + metrics.height)
        return SPLITTER_VERTICAL;
    if (studio.output_window_visible &&
        x >= (studio.workspace_visible ? studio.left_width + SPLITTER_SIZE : 0) &&
        y >= metrics.horizontal_y && y < metrics.horizontal_y + SPLITTER_SIZE)
        return SPLITTER_HORIZONTAL;
    return SPLITTER_NONE;
}

int studio_begin_splitter_drag(HWND window, int x, int y)
{
    LayoutMetrics metrics;

    studio.splitter_drag = studio_splitter_at(window, x, y);
    if (studio.splitter_drag == SPLITTER_NONE)
        return 0;
    metrics = get_layout_metrics(window);
    studio.splitter_drag_offset = studio.splitter_drag == SPLITTER_VERTICAL
                                      ? x - studio.left_width
                                      : y - metrics.horizontal_y;
    return 1;
}

void studio_update_splitter_drag(HWND window, int x, int y)
{
    LayoutMetrics metrics = get_layout_metrics(window);

    if (studio.splitter_drag == SPLITTER_VERTICAL) {
        studio.left_width = clamp_int(x - studio.splitter_drag_offset, MIN_TREE_WIDTH,
                                      metrics.width - MIN_EDITOR_WIDTH - SPLITTER_SIZE);
    } else if (studio.splitter_drag == SPLITTER_HORIZONTAL) {
        int splitter_y = y - studio.splitter_drag_offset;
        studio.bottom_height =
            clamp_int(metrics.top + metrics.height - splitter_y - SPLITTER_SIZE, MIN_BOTTOM_HEIGHT,
                      metrics.height - MIN_EDITOR_HEIGHT - SPLITTER_SIZE);
    }
    studio_layout_controls(window);
}

void studio_paint_splitters(HWND window)
{
    PAINTSTRUCT paint;
    LayoutMetrics metrics = get_layout_metrics(window);
    RECT vertical;
    RECT horizontal;
    HDC dc = BeginPaint(window, &paint);

    metrics.horizontal_y = metrics.top + metrics.height - studio.bottom_height - SPLITTER_SIZE;
    if (studio.workspace_visible) {
        SetRect(&vertical, studio.left_width, metrics.navigation_y,
                studio.left_width + SPLITTER_SIZE, metrics.top + metrics.height);
        FillRect(dc, &vertical, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dc, &vertical, EDGE_ETCHED, BF_LEFT | BF_RIGHT);
    }
    if (studio.output_window_visible) {
        SetRect(&horizontal, studio.workspace_visible ? studio.left_width + SPLITTER_SIZE : 0,
                metrics.horizontal_y, metrics.width, metrics.horizontal_y + SPLITTER_SIZE);
        FillRect(dc, &horizontal, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dc, &horizontal, EDGE_ETCHED, BF_TOP | BF_BOTTOM);
    }
    EndPaint(window, &paint);
}

HMENU studio_create_menu(void)
{
    HMENU main_menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    HMENU bookmarks = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    HMENU toolbars = CreatePopupMenu();
    HMENU insert = CreatePopupMenu();
    HMENU project = CreatePopupMenu();
    HMENU add_to_project = CreatePopupMenu();
    HMENU build = CreatePopupMenu();
    HMENU tools = CreatePopupMenu();
    HMENU window_menu = CreatePopupMenu();
    HMENU help = CreatePopupMenu();

    studio.view_menu = view;

    AppendMenuA(file, MF_STRING, ID_FILE_NEW_PROJECT, "New Project...");
    AppendMenuA(file, MF_STRING, ID_FILE_NEW_SCRIPT, "New JavaScript File...\tCtrl+N");
    AppendMenuA(file, MF_STRING, ID_FILE_OPEN_PROJECT, "Open Workspace...\tCtrl+Shift+O");
    AppendMenuA(file, MF_STRING, ID_FILE_OPEN_SCRIPT, "Open File...\tCtrl+O");
    AppendMenuA(file, MF_STRING, ID_FILE_CLOSE_WORKSPACE, "Close Workspace");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_SAVE, "Save\tCtrl+S");
    AppendMenuA(file, MF_STRING, ID_FILE_SAVE_AS, "Save As...");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_FILE_EXIT, "Exit");

    AppendMenuA(edit, MF_STRING, ID_EDIT_UNDO, "Undo\tCtrl+Z");
    AppendMenuA(edit, MF_STRING, ID_EDIT_REDO, "Redo\tCtrl+Y");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, ID_EDIT_CUT, "Cut\tCtrl+X");
    AppendMenuA(edit, MF_STRING, ID_EDIT_COPY, "Copy\tCtrl+C");
    AppendMenuA(edit, MF_STRING, ID_EDIT_PASTE, "Paste\tCtrl+V");
    AppendMenuA(edit, MF_STRING, ID_EDIT_DELETE, "Delete\tDel");
    AppendMenuA(edit, MF_STRING, ID_EDIT_SELECT_ALL, "Select All\tCtrl+A");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, ID_EDIT_FIND, "Find...\tCtrl+F");
    AppendMenuA(edit, MF_STRING, ID_EDIT_FIND_NEXT, "Find Next\tF3");
    AppendMenuA(edit, MF_STRING, ID_EDIT_FIND_IN_FILES, "Find in Files...\tCtrl+Shift+F");
    AppendMenuA(edit, MF_STRING, ID_EDIT_REPLACE, "Replace All...\tCtrl+H");
    AppendMenuA(edit, MF_STRING, ID_EDIT_GO_TO_LINE, "Go To...\tCtrl+G");
    AppendMenuA(edit, MF_STRING, ID_EDIT_AUTOCOMPLETE, "Complete Word\tCtrl+Space");
    AppendMenuA(bookmarks, MF_STRING, ID_EDIT_TOGGLE_BOOKMARK, "Toggle Bookmark\tCtrl+F2");
    AppendMenuA(bookmarks, MF_STRING, ID_EDIT_NEXT_BOOKMARK, "Next Bookmark\tF2");
    AppendMenuA(bookmarks, MF_STRING, ID_EDIT_PREVIOUS_BOOKMARK, "Previous Bookmark\tShift+F2");
    AppendMenuA(bookmarks, MF_STRING, ID_EDIT_CLEAR_BOOKMARKS, "Clear All Bookmarks");
    AppendMenuA(edit, MF_POPUP, (UINT_PTR)bookmarks, "Bookmarks");
    AppendMenuA(edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(edit, MF_STRING, ID_EDIT_GO_TO_DEFINITION, "Go To Definition\tF12");
    AppendMenuA(edit, MF_STRING, ID_EDIT_NAVIGATE_BACK, "Navigate Back\tAlt+Left");
    AppendMenuA(edit, MF_STRING, ID_EDIT_NAVIGATE_FORWARD, "Navigate Forward\tAlt+Right");

    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, ID_VIEW_TOOLBAR, "Standard and Debug");
    AppendMenuA(toolbars, MF_STRING | MF_CHECKED, ID_VIEW_WIZARDBAR, "WizardBar");
    AppendMenuA(view, MF_POPUP, (UINT_PTR)toolbars, "Toolbars");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL);
    AppendMenuA(view, MF_STRING | MF_CHECKED, ID_VIEW_WORKSPACE, "Workspace");
    AppendMenuA(view, MF_STRING | MF_CHECKED, ID_VIEW_OUTPUT_WINDOW, "Output Window");
    AppendMenuA(view, MF_STRING | MF_CHECKED, ID_VIEW_STATUS_BAR, "Status Bar");

    AppendMenuA(insert, MF_STRING, ID_FILE_NEW_SCRIPT, "New JavaScript File...");
    AppendMenuA(insert, MF_STRING, ID_INSERT_FILE_AS_TEXT, "File As Text...");

    AppendMenuA(project, MF_STRING, ID_FILE_OPEN_PROJECT, "Open Workspace...\tCtrl+Shift+O");
    AppendMenuA(project, MF_SEPARATOR, 0, NULL);
    AppendMenuA(add_to_project, MF_STRING, ID_FILE_NEW_SCRIPT, "New JavaScript File...");
    AppendMenuA(add_to_project, MF_STRING, ID_PROJECT_NEW_FOLDER, "New Folder...");
    AppendMenuA(add_to_project, MF_STRING, ID_PROJECT_ADD_FILES, "Files...");
    AppendMenuA(project, MF_POPUP, (UINT_PTR)add_to_project, "Add To Project");
    AppendMenuA(project, MF_STRING, ID_PROJECT_RENAME, "Rename\tF2");
    AppendMenuA(project, MF_STRING, ID_PROJECT_REMOVE, "Remove From Project\tDel");
    AppendMenuA(project, MF_STRING, ID_PROJECT_REFRESH, "Refresh");
    AppendMenuA(project, MF_SEPARATOR, 0, NULL);
    AppendMenuA(project, MF_STRING, ID_PROJECT_PROPERTIES, "Settings...\tAlt+F7");

    AppendMenuA(build, MF_STRING, ID_DEBUG_START, "Start Debug\tF5");
    AppendMenuA(build, MF_STRING, ID_DEBUG_STOP, "Stop Debugging\tShift+F5");
    AppendMenuA(build, MF_SEPARATOR, 0, NULL);
    AppendMenuA(build, MF_STRING, ID_DEBUG_BREAKPOINT, "Insert/Remove Breakpoint\tF9");
    AppendMenuA(build, MF_STRING, ID_DEBUG_STEP_OVER, "Step Over\tF10");
    AppendMenuA(build, MF_STRING, ID_DEBUG_STEP_INTO, "Step Into\tF11");
    AppendMenuA(build, MF_STRING, ID_DEBUG_STEP_OUT, "Step Out\tShift+F11");

    AppendMenuA(tools, MF_STRING, ID_EDIT_GO_TO_DEFINITION, "Go To Definition");
    AppendMenuA(tools, MF_STRING, ID_TOOLS_ADD_WATCH, "Add Selection To Watch");

    AppendMenuA(window_menu, MF_STRING, ID_VIEW_OUTPUT, "Output");
    AppendMenuA(window_menu, MF_STRING, ID_VIEW_CALLSTACK, "Call Stack");
    AppendMenuA(window_menu, MF_STRING, ID_VIEW_LOCALS, "Locals");
    AppendMenuA(window_menu, MF_STRING, ID_VIEW_WATCH, "Watch");

    AppendMenuA(help, MF_STRING, ID_HELP_ABOUT, "About QuickJS Studio...");

    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)file, "File");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)edit, "Edit");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)view, "View");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)insert, "Insert");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)project, "Project");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)build, "Build");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)tools, "Tools");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)window_menu, "Window");
    AppendMenuA(main_menu, MF_POPUP, (UINT_PTR)help, "Help");
    return main_menu;
}

static void select_toolbar_colors(HDC dc, HPEN pen, HBRUSH brush, HGDIOBJ *old_pen,
                                  HGDIOBJ *old_brush)
{
    *old_pen = SelectObject(dc, pen);
    *old_brush = SelectObject(dc, brush);
}

static void add_toolbar_icon(HIMAGELIST images, HDC screen, int icon)
{
    const COLORREF mask_color = RGB(192, 192, 192);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    HPEN outline = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
    HPEN blue_pen = CreatePen(PS_SOLID, 2, RGB(20, 70, 180));
    HBRUSH background = CreateSolidBrush(mask_color);
    HBRUSH yellow = CreateSolidBrush(RGB(255, 205, 45));
    HBRUSH blue = CreateSolidBrush(RGB(40, 105, 190));
    HBRUSH green = CreateSolidBrush(RGB(30, 180, 60));
    HBRUSH red = CreateSolidBrush(RGB(215, 35, 35));
    HBRUSH white = CreateSolidBrush(RGB(250, 250, 250));
    HGDIOBJ old_pen;
    HGDIOBJ old_brush;
    RECT area = {0, 0, 16, 16};

    FillRect(dc, &area, background);
    select_toolbar_colors(dc, outline, yellow, &old_pen, &old_brush);
    switch (icon) {
    case 0: {
        POINT folder[] = {{1, 5}, {5, 5}, {6, 3}, {11, 3}, {12, 5}, {15, 5}, {14, 13}, {1, 13}};
        Polygon(dc, folder, ARRAYSIZE(folder));
        MoveToEx(dc, 2, 7, NULL);
        LineTo(dc, 14, 7);
        break;
    }
    case 1:
        SelectObject(dc, blue);
        Rectangle(dc, 2, 1, 14, 15);
        SelectObject(dc, white);
        Rectangle(dc, 5, 2, 11, 7);
        Rectangle(dc, 5, 10, 12, 15);
        break;
    case 2: {
        POINT play[] = {{4, 2}, {13, 8}, {4, 14}};
        SelectObject(dc, green);
        Polygon(dc, play, ARRAYSIZE(play));
        break;
    }
    case 3:
        SelectObject(dc, red);
        Rectangle(dc, 3, 3, 13, 13);
        break;
    case 4:
        SelectObject(dc, red);
        Ellipse(dc, 3, 3, 13, 13);
        break;
    case 5:
        SelectObject(dc, yellow);
        Rectangle(dc, 3, 10, 12, 14);
        SelectObject(dc, blue_pen);
        MoveToEx(dc, 2, 5, NULL);
        LineTo(dc, 12, 5);
        LineTo(dc, 9, 2);
        MoveToEx(dc, 12, 5, NULL);
        LineTo(dc, 9, 8);
        break;
    case 6:
        SelectObject(dc, yellow);
        Rectangle(dc, 2, 12, 14, 15);
        SelectObject(dc, blue_pen);
        MoveToEx(dc, 8, 2, NULL);
        LineTo(dc, 8, 11);
        MoveToEx(dc, 4, 7, NULL);
        LineTo(dc, 8, 11);
        LineTo(dc, 12, 7);
        break;
    case 7:
        SelectObject(dc, yellow);
        Rectangle(dc, 2, 1, 14, 4);
        SelectObject(dc, blue_pen);
        MoveToEx(dc, 8, 13, NULL);
        LineTo(dc, 8, 4);
        MoveToEx(dc, 4, 8, NULL);
        LineTo(dc, 8, 4);
        LineTo(dc, 12, 8);
        break;
    case 8:
        SelectObject(dc, white);
        Ellipse(dc, 1, 2, 6, 7);
        Ellipse(dc, 1, 9, 6, 14);
        MoveToEx(dc, 5, 5, NULL);
        LineTo(dc, 14, 13);
        MoveToEx(dc, 5, 11, NULL);
        LineTo(dc, 14, 3);
        break;
    case 9:
        SelectObject(dc, white);
        Rectangle(dc, 5, 2, 14, 12);
        Rectangle(dc, 2, 5, 11, 15);
        break;
    case 10:
        SelectObject(dc, yellow);
        Rectangle(dc, 3, 3, 13, 15);
        SelectObject(dc, white);
        Rectangle(dc, 5, 5, 11, 13);
        SelectObject(dc, blue);
        Rectangle(dc, 6, 1, 10, 5);
        break;
    case 11:
        SelectObject(dc, white);
        Rectangle(dc, 3, 1, 13, 15);
        SelectObject(dc, blue_pen);
        MoveToEx(dc, 8, 4, NULL);
        LineTo(dc, 8, 12);
        MoveToEx(dc, 4, 8, NULL);
        LineTo(dc, 12, 8);
        break;
    case 12:
        SelectObject(dc, blue_pen);
        Arc(dc, 3, 3, 15, 14, 4, 12, 13, 4);
        MoveToEx(dc, 3, 4, NULL);
        LineTo(dc, 3, 10);
        LineTo(dc, 8, 7);
        break;
    case 13:
        SelectObject(dc, blue_pen);
        Arc(dc, 1, 3, 13, 14, 12, 12, 3, 4);
        MoveToEx(dc, 13, 4, NULL);
        LineTo(dc, 13, 10);
        LineTo(dc, 8, 7);
        break;
    case 14:
        SelectObject(dc, white);
        Ellipse(dc, 2, 2, 11, 11);
        SelectObject(dc, blue_pen);
        MoveToEx(dc, 10, 10, NULL);
        LineTo(dc, 15, 15);
        break;
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_bitmap);
    ImageList_AddMasked(images, bitmap, mask_color);
    DeleteObject(bitmap);
    DeleteObject(outline);
    DeleteObject(blue_pen);
    DeleteObject(background);
    DeleteObject(yellow);
    DeleteObject(blue);
    DeleteObject(green);
    DeleteObject(red);
    DeleteObject(white);
    DeleteDC(dc);
}

static void add_tree_icon(HIMAGELIST images, HDC screen, int icon)
{
    const COLORREF mask = RGB(192, 192, 192);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 16, 16);
    HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
    HPEN outline = CreatePen(PS_SOLID, 1, RGB(55, 55, 55));
    HBRUSH background = CreateSolidBrush(mask);
    HBRUSH yellow = CreateSolidBrush(RGB(255, 210, 45));
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH blue = CreateSolidBrush(RGB(70, 115, 210));
    HBRUSH cyan = CreateSolidBrush(RGB(40, 185, 180));
    HBRUSH purple = CreateSolidBrush(RGB(175, 75, 180));
    HGDIOBJ old_pen = SelectObject(dc, outline);
    HGDIOBJ old_brush = SelectObject(dc, yellow);
    RECT area = {0, 0, 16, 16};

    FillRect(dc, &area, background);
    if (icon == TREE_IMAGE_WORKSPACE || icon == TREE_IMAGE_FOLDER) {
        POINT folder[] = {{1, 5}, {5, 5}, {6, 3}, {11, 3}, {12, 5}, {15, 5}, {14, 13}, {1, 13}};
        Polygon(dc, folder, ARRAYSIZE(folder));
        MoveToEx(dc, 2, 7, NULL);
        LineTo(dc, 14, 7);
    } else if (icon == TREE_IMAGE_FILE) {
        SelectObject(dc, white);
        Rectangle(dc, 3, 1, 13, 15);
        MoveToEx(dc, 5, 5, NULL);
        LineTo(dc, 11, 5);
        MoveToEx(dc, 5, 8, NULL);
        LineTo(dc, 11, 8);
        MoveToEx(dc, 5, 11, NULL);
        LineTo(dc, 10, 11);
    } else if (icon == TREE_IMAGE_CLASS) {
        SelectObject(dc, cyan);
        Rectangle(dc, 2, 2, 14, 14);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(0, 0, 0));
        TextOutA(dc, 5, 2, "C", 1);
    } else if (icon == TREE_IMAGE_FUNCTION) {
        POINT diamond[] = {{8, 1}, {15, 8}, {8, 15}, {1, 8}};
        SelectObject(dc, purple);
        Polygon(dc, diamond, ARRAYSIZE(diamond));
    } else {
        SelectObject(dc, blue);
        Ellipse(dc, 3, 3, 13, 13);
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_bitmap);
    ImageList_AddMasked(images, bitmap, mask);
    DeleteObject(bitmap);
    DeleteObject(outline);
    DeleteObject(background);
    DeleteObject(yellow);
    DeleteObject(white);
    DeleteObject(blue);
    DeleteObject(cyan);
    DeleteObject(purple);
    DeleteDC(dc);
}

static HIMAGELIST create_tree_images(HWND parent)
{
    HDC screen = GetDC(parent);
    HIMAGELIST images = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 6, 0);
    for (int icon = 0; icon < 6; icon++)
        add_tree_icon(images, screen, icon);
    ReleaseDC(parent, screen);
    return images;
}

HWND studio_create_toolbar(HWND parent)
{
    static const char labels[] =
        "New Script\0Open Project\0Open Script\0Save\0Cut\0Copy\0Paste\0Start / Continue\0Stop\0Toggle Breakpoint\0Step Over\0Step Into\0Step Out\0Undo\0Redo\0Find\0";
    const BYTE icon_button = BTNS_BUTTON;
    TBBUTTON buttons[] = {{11, ID_FILE_NEW_SCRIPT, TBSTATE_ENABLED, icon_button, {0}, 0, 0},
                          {0, ID_FILE_OPEN_PROJECT, TBSTATE_ENABLED, icon_button, {0}, 0, 1},
                          {0, ID_FILE_OPEN_SCRIPT, TBSTATE_ENABLED, icon_button, {0}, 0, 2},
                          {1, ID_FILE_SAVE, TBSTATE_ENABLED, icon_button, {0}, 0, 3},
                          {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
                          {8, ID_EDIT_CUT, TBSTATE_ENABLED, icon_button, {0}, 0, 4},
                          {9, ID_EDIT_COPY, TBSTATE_ENABLED, icon_button, {0}, 0, 5},
                          {10, ID_EDIT_PASTE, TBSTATE_ENABLED, icon_button, {0}, 0, 6},
                          {12, ID_EDIT_UNDO, TBSTATE_ENABLED, icon_button, {0}, 0, 13},
                          {13, ID_EDIT_REDO, TBSTATE_ENABLED, icon_button, {0}, 0, 14},
                          {14, ID_EDIT_FIND, TBSTATE_ENABLED, icon_button, {0}, 0, 15},
                          {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
                          {2, ID_DEBUG_START, TBSTATE_ENABLED, icon_button, {0}, 0, 7},
                          {3, ID_DEBUG_STOP, TBSTATE_ENABLED, icon_button, {0}, 0, 8},
                          {4, ID_DEBUG_BREAKPOINT, TBSTATE_ENABLED, icon_button, {0}, 0, 9},
                          {0, 0, TBSTATE_ENABLED, BTNS_SEP, {0}, 0, 0},
                          {5, ID_DEBUG_STEP_OVER, TBSTATE_ENABLED, icon_button, {0}, 0, 10},
                          {6, ID_DEBUG_STEP_INTO, TBSTATE_ENABLED, icon_button, {0}, 0, 11},
                          {7, ID_DEBUG_STEP_OUT, TBSTATE_ENABLED, icon_button, {0}, 0, 12}};
    HWND toolbar =
        CreateWindowExA(0, TOOLBARCLASSNAMEA, NULL,
                        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 0, 0, 0,
                        0, parent, (HMENU)(INT_PTR)ID_TOOLBAR, GetModuleHandleA(NULL), NULL);
    HDC screen = GetDC(parent);
    studio.toolbar_images = ImageList_Create(16, 16, ILC_COLOR24 | ILC_MASK, 15, 0);
    for (int icon = 0; icon < 15; icon++)
        add_toolbar_icon(studio.toolbar_images, screen, icon);
    ReleaseDC(parent, screen);
    SendMessageA(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageA(toolbar, TB_SETIMAGELIST, 0, (LPARAM)studio.toolbar_images);
    SendMessageA(toolbar, TB_ADDSTRINGA, 0, (LPARAM)labels);
    SendMessageA(toolbar, TB_ADDBUTTONS, ARRAYSIZE(buttons), (LPARAM)buttons);
    SendMessageA(toolbar, TB_SETBUTTONWIDTH, 0, MAKELONG(24, 24));
    SendMessageA(toolbar, TB_SETBUTTONSIZE, 0, MAKELONG(24, 22));
    return toolbar;
}

HACCEL studio_create_accelerators(void)
{
    ACCEL accelerators[] = {{FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW_SCRIPT},
                            {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN_SCRIPT},
                            {FVIRTKEY | FCONTROL | FSHIFT, 'O', ID_FILE_OPEN_PROJECT},
                            {FVIRTKEY | FCONTROL, 'S', ID_FILE_SAVE},
                            {FVIRTKEY | FCONTROL, 'F', ID_EDIT_FIND},
                            {FVIRTKEY, VK_F3, ID_EDIT_FIND_NEXT},
                            {FVIRTKEY | FCONTROL | FSHIFT, 'F', ID_EDIT_FIND_IN_FILES},
                            {FVIRTKEY | FCONTROL, 'H', ID_EDIT_REPLACE},
                            {FVIRTKEY | FCONTROL, 'G', ID_EDIT_GO_TO_LINE},
                            {FVIRTKEY | FCONTROL, VK_SPACE, ID_EDIT_AUTOCOMPLETE},
                            {FVIRTKEY | FALT, VK_F7, ID_PROJECT_PROPERTIES},
                            {FVIRTKEY | FCONTROL, VK_F2, ID_EDIT_TOGGLE_BOOKMARK},
                            {FVIRTKEY, VK_F2, ID_EDIT_NEXT_BOOKMARK},
                            {FVIRTKEY | FSHIFT, VK_F2, ID_EDIT_PREVIOUS_BOOKMARK},
                            {FVIRTKEY, VK_F12, ID_EDIT_GO_TO_DEFINITION},
                            {FVIRTKEY | FALT, VK_LEFT, ID_EDIT_NAVIGATE_BACK},
                            {FVIRTKEY | FALT, VK_RIGHT, ID_EDIT_NAVIGATE_FORWARD},
                            {FVIRTKEY, VK_F5, ID_DEBUG_START},
                            {FVIRTKEY | FSHIFT, VK_F5, ID_DEBUG_STOP},
                            {FVIRTKEY, VK_F9, ID_DEBUG_BREAKPOINT},
                            {FVIRTKEY, VK_F10, ID_DEBUG_STEP_OVER},
                            {FVIRTKEY, VK_F11, ID_DEBUG_STEP_INTO},
                            {FVIRTKEY | FSHIFT, VK_F11, ID_DEBUG_STEP_OUT}};
    return CreateAcceleratorTableA(accelerators, ARRAYSIZE(accelerators));
}

void studio_create_child_controls(HWND window)
{
    TCITEMA tab = {0};
    DWORD list_style = WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;

    studio.toolbar = studio_create_toolbar(window);
    studio.wizard_scope = CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                              CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_HASSTRINGS,
                                          0, 0, 180, 240, window, (HMENU)(INT_PTR)ID_WIZARD_SCOPE,
                                          GetModuleHandleA(NULL), NULL);
    studio.wizard_member = CreateWindowExA(
        WS_EX_CLIENTEDGE, "COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0,
        240, 300, window, (HMENU)(INT_PTR)ID_WIZARD_MEMBER, GetModuleHandleA(NULL), NULL);
    studio.wizard_action = CreateWindowExA(
        0, "BUTTON", "Go", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 38, 23, window,
        (HMENU)(INT_PTR)ID_WIZARD_ACTION, GetModuleHandleA(NULL), NULL);
    studio.workspace_caption =
        CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0,
                        100, 20, window, NULL, GetModuleHandleA(NULL), NULL);
    studio.workspace_close = CreateWindowExA(
        0, "BUTTON", "x", WS_CHILD | WS_VISIBLE | BS_FLAT | BS_PUSHBUTTON, 0, 0, 18, 18, window,
        (HMENU)(INT_PTR)ID_WORKSPACE_CLOSE, GetModuleHandleA(NULL), NULL);
    studio.tree =
        CreateWindowExA(WS_EX_CLIENTEDGE, WC_TREEVIEWA, "",
                        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT, 0,
                        0, 100, 100, window, (HMENU)(INT_PTR)ID_TREE, GetModuleHandleA(NULL), NULL);
    studio.tree_images = create_tree_images(window);
    TreeView_SetImageList(studio.tree, studio.tree_images, TVSIL_NORMAL);
    studio.left_tabs =
        CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | TCS_BOTTOM, 0, 0, 100, 24,
                        window, (HMENU)(INT_PTR)ID_LEFT_TABS, GetModuleHandleA(NULL), NULL);
    TabCtrl_SetImageList(studio.left_tabs, studio.tree_images);
    tab.mask = TCIF_TEXT | TCIF_IMAGE;
    tab.pszText = "ClassView";
    tab.iImage = TREE_IMAGE_CLASS;
    TabCtrl_InsertItem(studio.left_tabs, PAGE_CLASS_VIEW, &tab);
    tab.pszText = "FileView";
    tab.iImage = TREE_IMAGE_FILE;
    TabCtrl_InsertItem(studio.left_tabs, PAGE_FILE_VIEW, &tab);
    studio.document_background =
        CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, window,
                        (HMENU)(INT_PTR)ID_DOCUMENT_BACKGROUND, GetModuleHandleA(NULL), NULL);
    studio.editor = editor_create(window, ID_EDITOR);
    studio.old_editor_proc =
        (WNDPROC)SetWindowLongPtrA(studio.editor, GWLP_WNDPROC, (LONG_PTR)studio_editor_proc);
    studio.tabs =
        CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | TCS_BOTTOM, 0, 0, 100, 100,
                        window, (HMENU)(INT_PTR)ID_BOTTOM_TABS, GetModuleHandleA(NULL), NULL);
    tab.mask = TCIF_TEXT;
    tab.pszText = "Output";
    TabCtrl_InsertItem(studio.tabs, PAGE_OUTPUT, &tab);
    tab.pszText = "Call Stack";
    TabCtrl_InsertItem(studio.tabs, PAGE_CALLSTACK, &tab);
    tab.pszText = "Locals";
    TabCtrl_InsertItem(studio.tabs, PAGE_LOCALS, &tab);
    tab.pszText = "Watch";
    TabCtrl_InsertItem(studio.tabs, PAGE_WATCH, &tab);
    studio.output = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "QuickJS Studio ready.\r\n",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 100,
        100, window, (HMENU)(INT_PTR)ID_OUTPUT, GetModuleHandleA(NULL), NULL);
    studio.old_output_proc =
        (WNDPROC)SetWindowLongPtrA(studio.output, GWLP_WNDPROC, (LONG_PTR)studio_output_proc);
    studio.callstack =
        CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "", list_style, 0, 0, 100, 100, window,
                        (HMENU)(INT_PTR)ID_CALLSTACK, GetModuleHandleA(NULL), NULL);
    studio.locals =
        CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "", list_style, 0, 0, 100, 100, window,
                        (HMENU)(INT_PTR)ID_LOCALS, GetModuleHandleA(NULL), NULL);
    studio.watch_edit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 100, 23, window,
        (HMENU)(INT_PTR)ID_WATCH_EDIT, GetModuleHandleA(NULL), NULL);
    studio.watch_add =
        CreateWindowExA(0, "BUTTON", "Add", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 53, 23,
                        window, (HMENU)(INT_PTR)ID_WATCH_ADD, GetModuleHandleA(NULL), NULL);
    studio.watch_list =
        CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "", list_style, 0, 0, 100, 100, window,
                        (HMENU)(INT_PTR)ID_WATCH_LIST, GetModuleHandleA(NULL), NULL);
    studio.status =
        CreateWindowExA(0, STATUSCLASSNAMEA, "Ready", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0,
                        0, 0, window, (HMENU)(INT_PTR)ID_STATUS, GetModuleHandleA(NULL), NULL);

    studio.classic_font = create_fixedsys_font();
    set_control_font(studio.wizard_scope, studio.classic_font);
    set_control_font(studio.wizard_member, studio.classic_font);
    set_control_font(studio.wizard_action, studio.classic_font);
    set_control_font(studio.workspace_caption, studio.classic_font);
    set_control_font(studio.workspace_close, studio.classic_font);
    set_control_font(studio.tree, studio.classic_font);
    set_control_font(studio.left_tabs, studio.classic_font);
    set_control_font(studio.tabs, studio.classic_font);
    set_control_font(studio.callstack, studio.classic_font);
    set_control_font(studio.locals, studio.classic_font);
    set_control_font(studio.watch_edit, studio.classic_font);
    set_control_font(studio.watch_add, studio.classic_font);
    set_control_font(studio.watch_list, studio.classic_font);
    set_control_font(studio.status, studio.classic_font);
    set_control_font(studio.output, studio.classic_font);
    studio_apply_system_window_colors();

    ListView_SetExtendedListViewStyle(studio.callstack, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ListView_SetExtendedListViewStyle(studio.locals, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    ListView_SetExtendedListViewStyle(studio.watch_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    list_add_column(studio.callstack, 0, 180, "Function");
    list_add_column(studio.callstack, 1, 380, "File");
    list_add_column(studio.callstack, 2, 60, "Line");
    list_add_column(studio.locals, 0, 180, "Name");
    list_add_column(studio.locals, 1, 360, "Value");
    list_add_column(studio.locals, 2, 90, "Type");
    list_add_column(studio.watch_list, 0, 180, "Expression");
    list_add_column(studio.watch_list, 1, 360, "Value");
    list_add_column(studio.watch_list, 2, 90, "Type");
    studio.old_watch_edit_proc = (WNDPROC)SetWindowLongPtrA(studio.watch_edit, GWLP_WNDPROC,
                                                            (LONG_PTR)studio_watch_edit_proc);
    SendMessageW(studio.watch_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Expression");
    studio_show_bottom_page(PAGE_OUTPUT);
}

void studio_set_view_check(int command, int checked)
{
    CheckMenuItem(studio.view_menu, (UINT)command,
                  MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

void studio_open_bottom_page(int page)
{
    studio.output_window_visible = 1;
    studio_set_view_check(ID_VIEW_OUTPUT_WINDOW, 1);
    studio_show_bottom_page(page);
    studio_layout_controls(studio.window);
}
