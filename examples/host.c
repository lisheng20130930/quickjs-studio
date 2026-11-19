#include "qjs_debug_agent.h"
#include "quickjs.h"

#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *data;
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = '\0';
    *size = (size_t)length;
    return data;
}

static JSValue engine_log(JSContext *context, JSValueConst this_value, int argc, JSValueConst *argv)
{
    const char *text = argc > 0 ? JS_ToCString(context, argv[0]) : NULL;
    (void)this_value;
    printf("[Engine] %s\n", text ? text : "");
    if (text)
        JS_FreeCString(context, text);
    return JS_UNDEFINED;
}

static void install_engine_binding(JSContext *context)
{
    JSValue global = JS_GetGlobalObject(context);
    JSValue engine = JS_NewObject(context);
    JS_SetPropertyStr(context, engine, "log", JS_NewCFunction(context, engine_log, "log", 1));
    JS_SetPropertyStr(context, global, "Engine", engine);
    JS_FreeValue(context, global);
}

static void print_exception(JSContext *context)
{
    JSValue exception = JS_GetException(context);
    const char *text = JS_ToCString(context, exception);
    fprintf(stderr, "JavaScript error: %s\n", text ? text : "unknown");
    if (text)
        JS_FreeCString(context, text);
    JS_FreeValue(context, exception);
}

int main(int argc, char **argv)
{
    JSRuntime *runtime;
    JSContext *context;
    int failed = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: qjs-host.exe script.js [script.js ...]\n");
        return 2;
    }

    runtime = JS_NewRuntime();
    context = runtime ? JS_NewContext(runtime) : NULL;
    if (!context) {
        JS_FreeRuntime(runtime);
        return 1;
    }
    install_engine_binding(context);
    if (qjs_debug_agent_connect_from_environment(context) < 0) {
        fprintf(stderr, "cannot connect to QuickJS Studio\n");
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        size_t source_size;
        char *source = read_file(argv[i], &source_size);
        JSValue result;
        if (!source) {
            fprintf(stderr, "cannot read %s\n", argv[i]);
            failed = 1;
            break;
        }
        result = JS_Eval(context, source, source_size, argv[i], JS_EVAL_TYPE_GLOBAL);
        free(source);
        if (JS_IsException(result)) {
            print_exception(context);
            JS_FreeValue(context, result);
            failed = 1;
            break;
        }
        JS_FreeValue(context, result);
    }
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    return failed;
}
