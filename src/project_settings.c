#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "project_settings.h"

enum { SETTINGS_ID_OK = 100, SETTINGS_ID_CANCEL, SETTINGS_ID_FIRST_EDIT };

typedef struct ProjectSettingsState {
    StudioProject *project;
    HWND edits[6];
    int accepted;
    int done;
} ProjectSettingsState;

static LRESULT CALLBACK project_settings_proc(HWND window, UINT message, WPARAM wparam,
                                              LPARAM lparam)
{
    static const char *labels[] = {"Executable:",  "Arguments:",    "Working directory:",
                                   "Script root:", "Entry script:", "Host API file:"};
    ProjectSettingsState *state = (ProjectSettingsState *)GetWindowLongPtrA(window, GWLP_USERDATA);

    if (message == WM_CREATE) {
        CREATESTRUCTA *create = (CREATESTRUCTA *)lparam;
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND control;
        const char *values[6];

        state = (ProjectSettingsState *)create->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)state);
        values[0] = state->project->executable;
        values[1] = state->project->arguments;
        values[2] = state->project->working_directory;
        values[3] = state->project->script_root;
        values[4] = state->project->entry_script;
        values[5] = state->project->host_api;
        for (int index = 0; index < 6; index++) {
            control =
                CreateWindowExA(0, "STATIC", labels[index], WS_CHILD | WS_VISIBLE, 12,
                                14 + index * 38, 118, 18, window, NULL, create->hInstance, NULL);
            SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
            state->edits[index] = CreateWindowExA(
                WS_EX_CLIENTEDGE, "EDIT", values[index],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 132, 10 + index * 38, 416, 23,
                window, (HMENU)(INT_PTR)(SETTINGS_ID_FIRST_EDIT + index), create->hInstance, NULL);
            SendMessageA(state->edits[index], WM_SETFONT, (WPARAM)font, TRUE);
        }
        control = CreateWindowExA(
            0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 392, 245, 75,
            24, window, (HMENU)(INT_PTR)SETTINGS_ID_OK, create->hInstance, NULL);
        SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
        control = CreateWindowExA(
            0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 473, 245, 75,
            24, window, (HMENU)(INT_PTR)SETTINGS_ID_CANCEL, create->hInstance, NULL);
        SendMessageA(control, WM_SETFONT, (WPARAM)font, TRUE);
        SetFocus(state->edits[0]);
        return 0;
    }
    if (!state)
        return DefWindowProcA(window, message, wparam, lparam);
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == SETTINGS_ID_OK) {
            GetWindowTextA(state->edits[0], state->project->executable,
                           sizeof(state->project->executable));
            GetWindowTextA(state->edits[1], state->project->arguments,
                           sizeof(state->project->arguments));
            GetWindowTextA(state->edits[2], state->project->working_directory,
                           sizeof(state->project->working_directory));
            GetWindowTextA(state->edits[3], state->project->script_root,
                           sizeof(state->project->script_root));
            GetWindowTextA(state->edits[4], state->project->entry_script,
                           sizeof(state->project->entry_script));
            GetWindowTextA(state->edits[5], state->project->host_api,
                           sizeof(state->project->host_api));
            state->accepted = 1;
            state->done = 1;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wparam) == SETTINGS_ID_CANCEL) {
            state->done = 1;
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        state->done = 1;
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int project_settings_show(HWND owner, StudioProject *project)
{
    static const char class_name[] = "QuickJSStudioProjectSettings";
    static int registered;
    ProjectSettingsState state = {0};
    WNDCLASSEXA window_class = {0};
    HINSTANCE instance = GetModuleHandleA(NULL);
    RECT owner_rect;
    MSG message;
    HWND window;
    int x;
    int y;

    if (!project)
        return 0;
    if (!registered) {
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = project_settings_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
        window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        window_class.lpszClassName = class_name;
        if (!RegisterClassExA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return 0;
        registered = 1;
    }
    state.project = project;
    GetWindowRect(owner, &owner_rect);
    x = owner_rect.left + ((owner_rect.right - owner_rect.left) - 576) / 2;
    y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - 312) / 2;
    window = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name, "Project Settings",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 576, 312, owner, NULL,
                             instance, &state);
    if (!window)
        return 0;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    while (!state.done && GetMessageA(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);
    return state.accepted;
}
