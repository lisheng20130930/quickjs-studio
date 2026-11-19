#include "quickjs-debugger.h"
#include "quickjs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MemoryTransport {
    char input[16384];
    size_t input_size;
    size_t input_offset;
    char output[65536];
    size_t output_size;
    int closed;
} MemoryTransport;

static void append_message(MemoryTransport *transport, const char *json)
{
    size_t length = strlen(json) + 1;
    int written = snprintf(transport->input + transport->input_size,
                           sizeof(transport->input) - transport->input_size, "%08x\n%s\n",
                           (unsigned)length, json);
    assert(written > 0);
    assert((size_t)written < sizeof(transport->input) - transport->input_size);
    transport->input_size += (size_t)written;
}

static size_t memory_read(void *opaque, char *buffer, size_t length)
{
    MemoryTransport *transport = opaque;
    size_t available = transport->input_size - transport->input_offset;
    if (available < length)
        return 0;
    memcpy(buffer, transport->input + transport->input_offset, length);
    transport->input_offset += length;
    return length;
}

static size_t memory_write(void *opaque, const char *buffer, size_t length)
{
    MemoryTransport *transport = opaque;
    if (length >= sizeof(transport->output) - transport->output_size)
        return 0;
    memcpy(transport->output + transport->output_size, buffer, length);
    transport->output_size += length;
    transport->output[transport->output_size] = '\0';
    return length;
}

static size_t memory_peek(void *opaque)
{
    MemoryTransport *transport = opaque;
    return transport->input_offset < transport->input_size;
}

static void memory_close(JSRuntime *runtime, void *opaque)
{
    MemoryTransport *transport = opaque;
    (void)runtime;
    transport->closed = 1;
}

static void test_top_level_breakpoint(void)
{
    static const char source[] = "globalThis.answer = 42;\n";
    MemoryTransport transport = {0};
    JSRuntime *runtime;
    JSContext *context;
    JSValue result;

    append_message(&transport, "{\"type\":\"breakpoints\",\"breakpoints\":{\"path\":"
                               "\"entry.js\",\"breakpoints\":[{\"line\":1}]}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":1,\"command\":\"continue\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":2,\"command\":\"stackTrace\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":3,\"command\":\"continue\"}}");

    runtime = JS_NewRuntime();
    assert(runtime != NULL);
    context = JS_NewContext(runtime);
    assert(context != NULL);
    js_debugger_attach(context, memory_read, memory_write, memory_peek, memory_close, &transport);
    result = JS_Eval(context, source, sizeof(source) - 1, "entry.js", JS_EVAL_TYPE_GLOBAL);
    assert(!JS_IsException(result));
    JS_FreeValue(context, result);

    if (!strstr(transport.output, "\"reason\":\"breakpoint\""))
        fprintf(stderr, "top-level debugger output:\n%s\n", transport.output);
    assert(strstr(transport.output, "\"reason\":\"breakpoint\"") != NULL);
    assert(strstr(transport.output, "\"filename\":\"entry.js\"") != NULL);
    assert(strstr(transport.output, "\"line\":1") != NULL);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}

int main(void)
{
    static const char library_source[] = "function add(a, b) {\n"
                                         "    const sum = a + b;\n"
                                         "    return sum;\n"
                                         "}\n";
    static const char main_source[] = "globalThis.answer = add(2, 3);\n";
    MemoryTransport transport = {0};
    JSRuntime *runtime;
    JSContext *context;
    JSValue result;

    append_message(&transport, "{\"type\":\"breakpoints\",\"breakpoints\":{\"path\":"
                               "\"library.js\",\"breakpoints\":[{\"line\":3}]}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":1,\"command\":\"continue\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":2,\"command\":\"stackTrace\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_seq\":3,\"command\":"
                               "\"variables\",\"args\":{\"variablesReference\":1}}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_seq\":4,\"command\":"
                               "\"evaluate\",\"args\":{\"frameId\":0,\"expression\":\"sum\"}}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":5,\"command\":\"next\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":6,\"command\":\"stackTrace\"}}");
    append_message(&transport, "{\"type\":\"request\",\"request\":{\"request_"
                               "seq\":7,\"command\":\"continue\"}}");

    runtime = JS_NewRuntime();
    assert(runtime != NULL);
    context = JS_NewContext(runtime);
    assert(context != NULL);

    js_debugger_attach(context, memory_read, memory_write, memory_peek, memory_close, &transport);
    result = JS_Eval(context, library_source, sizeof(library_source) - 1, "library.js",
                     JS_EVAL_TYPE_GLOBAL);
    assert(!JS_IsException(result));
    JS_FreeValue(context, result);
    result = JS_Eval(context, main_source, sizeof(main_source) - 1, "main.js", JS_EVAL_TYPE_GLOBAL);
    assert(!JS_IsException(result));
    JS_FreeValue(context, result);

    if (!strstr(transport.output, "\"reason\":\"breakpoint\""))
        fprintf(stderr, "debugger output:\n%s\n", transport.output);
    assert(strstr(transport.output, "\"reason\":\"breakpoint\"") != NULL);
    assert(strstr(transport.output, "\"filename\":\"library.js\"") != NULL);
    assert(strstr(transport.output, "\"filename\":\"main.js\"") != NULL);
    assert(strstr(transport.output, "\"name\":\"sum\"") != NULL);
    assert(strstr(transport.output, "\"request_seq\":4") != NULL);
    if (!strstr(transport.output, "\"result\":\"5\""))
        fprintf(stderr, "debugger output:\n%s\n", transport.output);
    assert(strstr(transport.output, "\"result\":\"5\"") != NULL);
    assert(strstr(transport.output, "\"reason\":\"step\"") != NULL);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    test_top_level_breakpoint();
    printf("quickjs debugger multi-file breakpoint/stack/locals/watch/step: PASS\n");
    return 0;
}
