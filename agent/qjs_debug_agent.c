#include "qjs_debug_agent.h"

#include "quickjs-debugger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

typedef struct PipeTransport {
    HANDLE pipe;
} PipeTransport;

static size_t pipe_read(void *opaque, char *buffer, size_t length)
{
    PipeTransport *transport = opaque;
    DWORD received = 0;
    if (length > MAXDWORD)
        length = MAXDWORD;
    if (!ReadFile(transport->pipe, buffer, (DWORD)length, &received, NULL))
        return 0;
    return received;
}

static size_t pipe_write(void *opaque, const char *buffer, size_t length)
{
    PipeTransport *transport = opaque;
    DWORD written = 0;
    if (length > MAXDWORD)
        length = MAXDWORD;
    if (!WriteFile(transport->pipe, buffer, (DWORD)length, &written, NULL))
        return 0;
    return written;
}

static size_t pipe_peek(void *opaque)
{
    PipeTransport *transport = opaque;
    DWORD available = 0;
    if (!PeekNamedPipe(transport->pipe, NULL, 0, NULL, &available, NULL))
        return (size_t)-1;
    return available != 0;
}

static void pipe_close(JSRuntime *runtime, void *opaque)
{
    PipeTransport *transport = opaque;
    (void)runtime;
    if (transport) {
        if (transport->pipe != INVALID_HANDLE_VALUE)
            CloseHandle(transport->pipe);
        free(transport);
    }
}

int qjs_debug_agent_connect_from_environment(JSContext *context)
{
    const char *name = getenv("QJS_STUDIO_PIPE");
    char path[512];
    HANDLE pipe;
    PipeTransport *transport;

    if (!name || !name[0])
        return 0;
    if (strncmp(name, "\\\\.\\pipe\\", 9) == 0) {
        if (strlen(name) >= sizeof(path))
            return -1;
        strcpy(path, name);
    } else {
        if (snprintf(path, sizeof(path), "\\\\.\\pipe\\%s", name) < 0 ||
            strlen(path) >= sizeof(path) - 1)
            return -1;
    }

    pipe = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
        return -1;

    transport = calloc(1, sizeof(*transport));
    if (!transport) {
        CloseHandle(pipe);
        return -1;
    }
    transport->pipe = pipe;
    js_debugger_attach(context, pipe_read, pipe_write, pipe_peek, pipe_close, transport);
    return 1;
}

#else

int qjs_debug_agent_connect_from_environment(JSContext *context)
{
    (void)context;
    return 0;
}

#endif
