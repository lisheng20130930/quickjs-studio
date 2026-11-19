#ifndef QJS_STUDIO_INPUT_DIALOG_H
#define QJS_STUDIO_INPUT_DIALOG_H

#include <windows.h>

#include <stddef.h>

int input_dialog_show(HWND owner, const char *title, const char *label, const char *initial,
                      char *value, size_t capacity);

#endif
