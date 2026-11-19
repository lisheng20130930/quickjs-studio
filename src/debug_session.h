#ifndef QJS_STUDIO_DEBUG_SESSION_H
#define QJS_STUDIO_DEBUG_SESSION_H

#include <windows.h>

#define WM_QJS_DEBUG_CONNECTED (WM_APP + 10)
#define WM_QJS_DEBUG_MESSAGE (WM_APP + 11)
#define WM_QJS_DEBUG_TERMINATED (WM_APP + 12)
#define WM_QJS_DEBUG_OUTPUT (WM_APP + 13)
#define WM_QJS_DEBUG_TRACE (WM_APP + 14)

typedef struct DebugSession {
    HWND window;
    HANDLE pipe;
    HANDLE process;
    HANDLE process_thread;
    HANDLE reader_thread;
    HANDLE writer_thread;
    HANDLE write_event;
    HANDLE output_read;
    HANDLE output_thread;
    CRITICAL_SECTION write_lock;
    void *write_head;
    void *write_tail;
    LONG next_sequence;
    volatile LONG connected;
    volatile LONG stopping;
    int lock_initialized;
} DebugSession;

int debug_session_start(DebugSession *session, HWND window, const char *executable,
                        const char *arguments, const char *working_directory);
void debug_session_stop(DebugSession *session);
int debug_session_is_running(const DebugSession *session);
int debug_session_send_breakpoints(DebugSession *session, const char *path, const int *lines,
                                   int count);
int debug_session_send_command(DebugSession *session, const char *command, int frame,
                               const char *expression, int variables_reference);
void debug_session_free_message(char *message);

#endif
