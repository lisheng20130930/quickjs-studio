#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "input_dialog.h"

#include <stdio.h>

enum { INPUT_ID_EDIT = 100, INPUT_ID_OK, INPUT_ID_CANCEL };

typedef struct InputDialogState {
    HWND edit;
    const char *label;
    const char *initial;
    char *value;
    size_t capacity;
    int accepted;
    int done;
} InputDialogState;

static LRESULT CALLBACK input_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    InputDialogState *state = (InputDialogState *)GetWindowLongPtrA(window, GWLP_USERDATA);

    if (message == WM_CREATE) {
        CREATESTRUCTA *create = (CREATESTRUCTA *)lparam;
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND label;
        HWND ok;
        HWND cancel;

        state = (InputDialogState *)create->lpCreateParams;
        SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)state);
        label = CreateWindowExA(0, "STATIC", state->label, WS_CHILD | WS_VISIBLE, 12, 12, 356, 18,
                                window, NULL, create->hInstance, NULL);
        state->edit =
            CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", state->initial,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 34, 356, 23,
                            window, (HMENU)(INT_PTR)INPUT_ID_EDIT, create->hInstance, NULL);
        ok = CreateWindowExA(0, "BUTTON", "OK",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 212, 72, 75, 24,
                             window, (HMENU)(INT_PTR)INPUT_ID_OK, create->hInstance, NULL);
        cancel = CreateWindowExA(
            0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 293, 72, 75,
            24, window, (HMENU)(INT_PTR)INPUT_ID_CANCEL, create->hInstance, NULL);
        SendMessageA(label, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(state->edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(ok, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(cancel, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(state->edit, EM_SETSEL, 0, -1);
        SetFocus(state->edit);
        return 0;
    }
    if (!state)
        return DefWindowProcA(window, message, wparam, lparam);
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wparam) == INPUT_ID_OK) {
            GetWindowTextA(state->edit, state->value, (int)state->capacity);
            state->accepted = 1;
            state->done = 1;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wparam) == INPUT_ID_CANCEL) {
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

int input_dialog_show(HWND owner, const char *title, const char *label, const char *initial,
                      char *value, size_t capacity)
{
    static const char class_name[] = "QuickJSStudioInputDialog";
    static int registered;
    InputDialogState state = {0};
    WNDCLASSEXA window_class = {0};
    HINSTANCE instance = GetModuleHandleA(NULL);
    RECT owner_rect;
    MSG message;
    HWND window;
    char initial_copy[1024];
    int x;
    int y;

    if (!value || capacity == 0)
        return 0;
    snprintf(initial_copy, sizeof(initial_copy), "%s", initial ? initial : "");
    value[0] = '\0';
    if (!registered) {
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = input_dialog_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
        window_class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        window_class.lpszClassName = class_name;
        if (!RegisterClassExA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return 0;
        registered = 1;
    }
    state.label = label ? label : "Value:";
    state.initial = initial_copy;
    state.value = value;
    state.capacity = capacity;
    GetWindowRect(owner, &owner_rect);
    x = owner_rect.left + ((owner_rect.right - owner_rect.left) - 392) / 2;
    y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - 132) / 2;
    window = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name, title ? title : "QuickJS Studio",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 392, 132, owner, NULL,
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
