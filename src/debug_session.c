#include "debug_session.h"

#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PendingWrite {
    struct PendingWrite *next;
    DWORD length;
    char data[];
} PendingWrite;

static void post_trace(DebugSession *session, const char *format, ...)
{
    char buffer[4096];
    char *text;
    size_t length;
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    length = strlen(buffer) + 1;
    text = HeapAlloc(GetProcessHeap(), 0, length);
    if (!text)
        return;
    memcpy(text, buffer, length);
    if (!PostMessageA(session->window, WM_QJS_DEBUG_TRACE, 0, (LPARAM)text))
        HeapFree(GetProcessHeap(), 0, text);
}

static int read_fully(DebugSession *session, void *buffer, DWORD length)
{
    BYTE *cursor = buffer;
    while (length) {
        DWORD available = 0;
        DWORD received = 0;
        DWORD chunk;
        if (session->stopping)
            return 0;
        if (!PeekNamedPipe(session->pipe, NULL, 0, NULL, &available, NULL))
            return 0;
        if (!available) {
            Sleep(2);
            continue;
        }
        chunk = length < available ? length : available;
        if (!ReadFile(session->pipe, cursor, chunk, &received, NULL) || !received)
            return 0;
        cursor += received;
        length -= received;
    }
    return 1;
}

static int write_fully(HANDLE pipe, const void *buffer, DWORD length)
{
    const BYTE *cursor = buffer;
    while (length) {
        DWORD written = 0;
        if (!WriteFile(pipe, cursor, length, &written, NULL) || !written)
            return 0;
        cursor += written;
        length -= written;
    }
    return 1;
}

static PendingWrite *take_pending_write(DebugSession *session)
{
    PendingWrite *item;
    EnterCriticalSection(&session->write_lock);
    item = session->write_head;
    if (item) {
        session->write_head = item->next;
        if (!session->write_head)
            session->write_tail = NULL;
    }
    LeaveCriticalSection(&session->write_lock);
    return item;
}

static DWORD WINAPI writer_main(void *opaque)
{
    DebugSession *session = opaque;
    post_trace(session, "[transport] writer thread started");
    for (;;) {
        WaitForSingleObject(session->write_event, INFINITE);
        for (;;) {
            PendingWrite *item = take_pending_write(session);
            if (!item)
                break;
            if (!session->stopping) {
                if (!write_fully(session->pipe, item->data, item->length)) {
                    post_trace(session, "[transport] send failed, error=%lu",
                               (unsigned long)GetLastError());
                    InterlockedExchange(&session->stopping, 1);
                } else {
                    post_trace(session, "[transport] sent %lu bytes", (unsigned long)item->length);
                }
            }
            HeapFree(GetProcessHeap(), 0, item);
        }
        if (session->stopping)
            break;
    }
    post_trace(session, "[transport] writer thread stopped");
    return 0;
}

static DWORD WINAPI reader_main(void *opaque)
{
    DebugSession *session = opaque;
    post_trace(session, "[transport] waiting for target process");
    BOOL connected = ConnectNamedPipe(session->pipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        post_trace(session, "[transport] connect failed, error=%lu", (unsigned long)GetLastError());
        PostMessageA(session->window, WM_QJS_DEBUG_TERMINATED, 0, 0);
        return 0;
    }
    InterlockedExchange(&session->connected, 1);
    post_trace(session, "[transport] target connected");
    PostMessageA(session->window, WM_QJS_DEBUG_CONNECTED, 0, 0);

    for (;;) {
        char header[10] = {0};
        char *message;
        unsigned long length;
        if (!read_fully(session, header, 9)) {
            post_trace(session, "[transport] receive header ended, error=%lu",
                       (unsigned long)GetLastError());
            break;
        }
        header[8] = '\0';
        length = strtoul(header, NULL, 16);
        if (!length || length > 4 * 1024 * 1024) {
            post_trace(session, "[protocol] invalid message length=%lu", length);
            break;
        }
        message = HeapAlloc(GetProcessHeap(), 0, length + 1);
        if (!message)
            break;
        if (!read_fully(session, message, (DWORD)length)) {
            HeapFree(GetProcessHeap(), 0, message);
            break;
        }
        message[length] = '\0';
        while (length && (message[length - 1] == '\n' || message[length - 1] == '\r'))
            message[--length] = '\0';
        post_trace(session, "[protocol] <- %s", message);
        if (!PostMessageA(session->window, WM_QJS_DEBUG_MESSAGE, 0, (LPARAM)message))
            HeapFree(GetProcessHeap(), 0, message);
    }
    PostMessageA(session->window, WM_QJS_DEBUG_TERMINATED, 0, 0);
    return 0;
}

static DWORD WINAPI output_main(void *opaque)
{
    DebugSession *session = opaque;
    char buffer[1024];
    DWORD received;

    while (ReadFile(session->output_read, buffer, sizeof(buffer) - 1, &received, NULL) &&
           received) {
        char *text = HeapAlloc(GetProcessHeap(), 0, received + 1);
        if (!text)
            break;
        memcpy(text, buffer, received);
        text[received] = '\0';
        if (!PostMessageA(session->window, WM_QJS_DEBUG_OUTPUT, 0, (LPARAM)text))
            HeapFree(GetProcessHeap(), 0, text);
    }
    return 0;
}

static int send_json(DebugSession *session, const cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    size_t length;
    PendingWrite *item;
    if (!session || !session->pipe || !session->lock_initialized || !session->write_event ||
        !session->connected || session->stopping || !text) {
        free(text);
        return 0;
    }
    length = strlen(text) + 1;
    if (length > 0xffffffffu - 9) {
        free(text);
        return 0;
    }
    item = HeapAlloc(GetProcessHeap(), 0, sizeof(*item) + 9 + length);
    if (!item) {
        free(text);
        return 0;
    }
    item->next = NULL;
    item->length = (DWORD)(9 + length);
    snprintf(item->data, 10, "%08x\n", (unsigned)length);
    memcpy(item->data + 9, text, length - 1);
    item->data[9 + length - 1] = '\n';
    post_trace(session, "[protocol] -> %s", text);
    free(text);

    EnterCriticalSection(&session->write_lock);
    if (session->write_tail)
        ((PendingWrite *)session->write_tail)->next = item;
    else
        session->write_head = item;
    session->write_tail = item;
    LeaveCriticalSection(&session->write_lock);
    SetEvent(session->write_event);
    return 1;
}

int debug_session_start(DebugSession *session, HWND window, const char *executable,
                        const char *arguments, const char *working_directory)
{
    char pipe_name[128];
    char command_line[4096];
    char previous_pipe[512];
    DWORD previous_size;
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    HANDLE output_write = NULL;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};

    debug_session_stop(session);
    memset(session, 0, sizeof(*session));
    session->window = window;
    session->next_sequence = 1;
    InitializeCriticalSection(&session->write_lock);
    session->lock_initialized = 1;
    session->write_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!session->write_event) {
        debug_session_stop(session);
        return 0;
    }

    snprintf(pipe_name, sizeof(pipe_name), "qjs-studio-%lu-%lu",
             (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    snprintf(command_line, sizeof(command_line), "\\\\.\\pipe\\%s", pipe_name);
    session->pipe =
        CreateNamedPipeA(command_line, PIPE_ACCESS_DUPLEX,
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, NULL);
    if (session->pipe == INVALID_HANDLE_VALUE) {
        session->pipe = NULL;
        debug_session_stop(session);
        return 0;
    }

    previous_size =
        GetEnvironmentVariableA("QJS_STUDIO_PIPE", previous_pipe, sizeof(previous_pipe));
    SetEnvironmentVariableA("QJS_STUDIO_PIPE", pipe_name);
    snprintf(command_line, sizeof(command_line), "\"%s\" %s", executable,
             arguments ? arguments : "");
    startup.cb = sizeof(startup);
    if (!CreatePipe(&session->output_read, &output_write, &security, 0) ||
        !SetHandleInformation(session->output_read, HANDLE_FLAG_INHERIT, 0)) {
        if (output_write)
            CloseHandle(output_write);
        if (previous_size && previous_size < sizeof(previous_pipe))
            SetEnvironmentVariableA("QJS_STUDIO_PIPE", previous_pipe);
        else
            SetEnvironmentVariableA("QJS_STUDIO_PIPE", NULL);
        debug_session_stop(session);
        return 0;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    if (!CreateProcessA(executable, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        working_directory, &startup, &process)) {
        CloseHandle(output_write);
        if (previous_size && previous_size < sizeof(previous_pipe))
            SetEnvironmentVariableA("QJS_STUDIO_PIPE", previous_pipe);
        else
            SetEnvironmentVariableA("QJS_STUDIO_PIPE", NULL);
        debug_session_stop(session);
        return 0;
    }
    CloseHandle(output_write);
    if (previous_size && previous_size < sizeof(previous_pipe))
        SetEnvironmentVariableA("QJS_STUDIO_PIPE", previous_pipe);
    else
        SetEnvironmentVariableA("QJS_STUDIO_PIPE", NULL);

    session->process = process.hProcess;
    session->process_thread = process.hThread;
    post_trace(session, "[process] started pid=%lu executable=%s",
               (unsigned long)process.dwProcessId, executable);
    session->reader_thread = CreateThread(NULL, 0, reader_main, session, 0, NULL);
    session->writer_thread = CreateThread(NULL, 0, writer_main, session, 0, NULL);
    session->output_thread = CreateThread(NULL, 0, output_main, session, 0, NULL);
    if (!session->reader_thread || !session->writer_thread || !session->output_thread) {
        debug_session_stop(session);
        return 0;
    }
    return 1;
}

void debug_session_stop(DebugSession *session)
{
    if (!session)
        return;
    InterlockedExchange(&session->stopping, 1);
    if (session->process && WaitForSingleObject(session->process, 0) == WAIT_TIMEOUT)
        TerminateProcess(session->process, 0);
    if (session->reader_thread)
        CancelSynchronousIo(session->reader_thread);
    if (session->writer_thread)
        CancelSynchronousIo(session->writer_thread);
    if (session->output_thread)
        CancelSynchronousIo(session->output_thread);
    if (session->pipe) {
        DisconnectNamedPipe(session->pipe);
        CloseHandle(session->pipe);
        session->pipe = NULL;
    }
    if (session->reader_thread) {
        WaitForSingleObject(session->reader_thread, 1000);
        CloseHandle(session->reader_thread);
        session->reader_thread = NULL;
    }
    if (session->write_event)
        SetEvent(session->write_event);
    if (session->writer_thread) {
        WaitForSingleObject(session->writer_thread, 1000);
        CloseHandle(session->writer_thread);
        session->writer_thread = NULL;
    }
    if (session->output_read) {
        CloseHandle(session->output_read);
        session->output_read = NULL;
    }
    if (session->output_thread) {
        WaitForSingleObject(session->output_thread, 1000);
        CloseHandle(session->output_thread);
        session->output_thread = NULL;
    }
    if (session->process_thread) {
        CloseHandle(session->process_thread);
        session->process_thread = NULL;
    }
    if (session->process) {
        CloseHandle(session->process);
        session->process = NULL;
    }
    if (session->write_event) {
        CloseHandle(session->write_event);
        session->write_event = NULL;
    }
    if (session->lock_initialized) {
        PendingWrite *item;
        while ((item = take_pending_write(session)) != NULL)
            HeapFree(GetProcessHeap(), 0, item);
        DeleteCriticalSection(&session->write_lock);
        session->lock_initialized = 0;
    }
    session->window = NULL;
}

int debug_session_is_running(const DebugSession *session)
{
    return session && session->process && WaitForSingleObject(session->process, 0) == WAIT_TIMEOUT;
}

int debug_session_send_breakpoints(DebugSession *session, const char *path, const int *lines,
                                   int count)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *body = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    int ok;

    cJSON_AddStringToObject(root, "type", "breakpoints");
    cJSON_AddItemToObject(root, "breakpoints", body);
    cJSON_AddStringToObject(body, "path", path);
    cJSON_AddItemToObject(body, "breakpoints", array);
    for (int i = 0; i < count; i++) {
        cJSON *breakpoint = cJSON_CreateObject();
        cJSON_AddNumberToObject(breakpoint, "line", lines[i]);
        cJSON_AddItemToArray(array, breakpoint);
    }
    ok = send_json(session, root);
    cJSON_Delete(root);
    return ok;
}

int debug_session_send_command(DebugSession *session, const char *command, int frame,
                               const char *expression, int variables_reference)
{
    int sequence = (int)InterlockedIncrement(&session->next_sequence);
    cJSON *root = cJSON_CreateObject();
    cJSON *request = cJSON_CreateObject();
    cJSON *args = NULL;
    int ok;

    cJSON_AddStringToObject(root, "type", "request");
    cJSON_AddItemToObject(root, "request", request);
    cJSON_AddNumberToObject(request, "request_seq", sequence);
    cJSON_AddStringToObject(request, "command", command);
    if (frame >= 0 || expression || variables_reference >= 0) {
        args = cJSON_CreateObject();
        cJSON_AddItemToObject(request, "args", args);
        if (frame >= 0)
            cJSON_AddNumberToObject(args, "frameId", frame);
        if (expression)
            cJSON_AddStringToObject(args, "expression", expression);
        if (variables_reference >= 0)
            cJSON_AddNumberToObject(args, "variablesReference", variables_reference);
    }
    ok = send_json(session, root);
    cJSON_Delete(root);
    return ok ? sequence : 0;
}

void debug_session_free_message(char *message)
{
    if (message)
        HeapFree(GetProcessHeap(), 0, message);
}
