/*
 * QuickJS debugger accessors.
 *
 * This file is included by quickjs.c because these operations need the
 * interpreter's private stack-frame and bytecode structures.  Keeping them
 * together makes the small QuickJS patch easy to review when upgrading.
 */

JSDebuggerLocation js_debugger_current_location(JSContext *ctx, const uint8_t *cur_pc)
{
    JSDebuggerLocation location = {0};
    JSStackFrame *sf = ctx->rt->current_stack_frame;
    JSObject *p;
    JSFunctionBytecode *b;
    const uint8_t *pc;

    if (!sf)
        return location;
    p = JS_VALUE_GET_OBJ(sf->cur_func);
    if (!p || !js_class_has_bytecode(p->class_id))
        return location;
    b = p->u.func.function_bytecode;
    if (!b || !b->has_debug)
        return location;

    pc = cur_pc ? cur_pc : sf->cur_pc;
    if (!pc || pc <= b->byte_code_buf || pc > b->byte_code_buf + b->byte_code_len)
        return location;

    location.line = find_line_num(ctx, b, pc - b->byte_code_buf - 1);
    location.filename = b->debug.filename;
    return location;
}

JSDebuggerInfo *js_debugger_info(JSRuntime *rt)
{
    return &rt->debugger_info;
}

uint32_t js_debugger_stack_depth(JSContext *ctx)
{
    uint32_t depth = 0;
    JSStackFrame *sf;

    for (sf = ctx->rt->current_stack_frame; sf; sf = sf->prev_frame)
        depth++;
    return depth;
}

JSValue js_debugger_build_backtrace(JSContext *ctx, const uint8_t *cur_pc)
{
    JSStackFrame *sf;
    JSValue frames = JS_NewArray(ctx);
    uint32_t stack_index = 0;

    for (sf = ctx->rt->current_stack_frame; sf; sf = sf->prev_frame) {
        JSValue frame = JS_NewObject(ctx);
        const char *name = get_func_name(ctx, sf->cur_func);
        JSObject *p = JS_VALUE_GET_OBJ(sf->cur_func);

        JS_SetPropertyStr(ctx, frame, "id", JS_NewUint32(ctx, stack_index));
        JS_SetPropertyStr(ctx, frame, "name",
                          JS_NewString(ctx, name && name[0] ? name : "<anonymous>"));
        if (name)
            JS_FreeCString(ctx, name);

        if (p && js_class_has_bytecode(p->class_id)) {
            JSFunctionBytecode *b = p->u.func.function_bytecode;
            const uint8_t *pc =
                (sf == ctx->rt->current_stack_frame && cur_pc) ? cur_pc : sf->cur_pc;
            if (b->has_debug && pc && pc > b->byte_code_buf &&
                pc <= b->byte_code_buf + b->byte_code_len) {
                int line = find_line_num(ctx, b, pc - b->byte_code_buf - 1);
                JS_SetPropertyStr(ctx, frame, "filename", JS_AtomToString(ctx, b->debug.filename));
                if (line >= 0)
                    JS_SetPropertyStr(ctx, frame, "line", JS_NewInt32(ctx, line));
            }
        } else {
            JS_SetPropertyStr(ctx, frame, "name", JS_NewString(ctx, "(native)"));
        }

        JS_SetPropertyUint32(ctx, frames, stack_index++, frame);
    }
    return frames;
}

int js_debugger_check_breakpoint(JSContext *ctx, uint32_t current_dirty, const uint8_t *cur_pc)
{
    JSValue path_data = JS_UNDEFINED;
    JSValue breakpoints = JS_UNDEFINED;
    JSStackFrame *sf = ctx->rt->current_stack_frame;
    JSObject *f;
    JSFunctionBytecode *b;
    JSDebuggerLocation location;
    const char *filename;
    uint32_t count = 0;
    int matched = 0;

    (void)current_dirty;

    if (!sf)
        return 0;
    f = JS_VALUE_GET_OBJ(sf->cur_func);
    if (!f || !js_class_has_bytecode(f->class_id))
        return 0;
    b = f->u.func.function_bytecode;
    if (!b->has_debug || !b->debug.filename)
        return 0;

    location = js_debugger_current_location(ctx, cur_pc);
    if (!location.filename || location.line <= 0)
        return 0;
    filename = JS_AtomToCString(ctx, location.filename);
    if (!filename)
        return 0;
    path_data = js_debugger_file_breakpoints(ctx, filename);
    JS_FreeCString(ctx, filename);
    if (JS_IsUndefined(path_data))
        goto done;

    breakpoints = JS_GetPropertyStr(ctx, path_data, "breakpoints");
    JSValue length = JS_GetPropertyStr(ctx, breakpoints, "length");
    JS_ToUint32(ctx, &count, length);
    JS_FreeValue(ctx, length);
    for (uint32_t i = 0; i < count; i++) {
        JSValue breakpoint = JS_GetPropertyUint32(ctx, breakpoints, i);
        JSValue line = JS_GetPropertyStr(ctx, breakpoint, "line");
        uint32_t requested = 0;
        JS_ToUint32(ctx, &requested, line);
        JS_FreeValue(ctx, line);
        JS_FreeValue(ctx, breakpoint);
        if (requested == (uint32_t)location.line) {
            matched = 1;
            break;
        }
    }

done:
    JS_FreeValue(ctx, breakpoints);
    JS_FreeValue(ctx, path_data);
    return matched;
}

JSValue js_debugger_local_variables(JSContext *ctx, int stack_index)
{
    JSValue result = JS_NewObject(ctx);
    JSStackFrame *sf;
    int current = 0;

    if (stack_index == 0 && !JS_IsNull(ctx->rt->current_exception) &&
        !JS_IsUndefined(ctx->rt->current_exception)) {
        JS_SetPropertyStr(ctx, result, "<exception>", JS_DupValue(ctx, ctx->rt->current_exception));
    }

    for (sf = ctx->rt->current_stack_frame; sf; sf = sf->prev_frame) {
        JSObject *f;
        JSFunctionBytecode *b;

        if (current++ < stack_index)
            continue;
        f = JS_VALUE_GET_OBJ(sf->cur_func);
        if (!f || !js_class_has_bytecode(f->class_id))
            break;
        b = f->u.func.function_bytecode;

        for (uint32_t i = 0; i < b->arg_count + b->var_count; i++) {
            JSValue value = i < b->arg_count ? sf->arg_buf[i] : sf->var_buf[i - b->arg_count];
            if (!JS_IsUninitialized(value))
                JS_SetProperty(ctx, result, b->vardefs[i].var_name, JS_DupValue(ctx, value));
        }
        break;
    }
    return result;
}

JSValue js_debugger_closure_variables(JSContext *ctx, int stack_index)
{
    JSValue result = JS_NewObject(ctx);
    JSStackFrame *sf;
    int current = 0;

    for (sf = ctx->rt->current_stack_frame; sf; sf = sf->prev_frame) {
        JSObject *f;
        JSFunctionBytecode *b;

        if (current++ < stack_index)
            continue;
        f = JS_VALUE_GET_OBJ(sf->cur_func);
        if (!f || !js_class_has_bytecode(f->class_id))
            break;
        b = f->u.func.function_bytecode;

        for (uint32_t i = 0; i < b->closure_var_count; i++) {
            JSVarRef *ref = f->u.func.var_refs ? f->u.func.var_refs[i] : NULL;
            JSValue value;
            if (!ref || !ref->pvalue)
                continue;
            value = *ref->pvalue;
            if (!JS_IsUninitialized(value))
                JS_SetProperty(ctx, result, b->closure_var[i].var_name, JS_DupValue(ctx, value));
        }
        break;
    }
    return result;
}

static JSValue js_debugger_eval(JSContext *ctx, JSStackFrame *sf, const char *input,
                                size_t input_len, const char *filename, int flags)
{
    JSParseState state, *s = &state;
    JSFunctionDef *fd;
    JSFunctionBytecode *b;
    JSVarRef **var_refs;
    JSValue fun_obj;
    JSObject *p;

    js_parse_init(ctx, s, input, input_len, filename);
    skip_shebang(&s->buf_ptr, s->buf_end);
    if (!sf || JS_VALUE_GET_TAG(sf->cur_func) != JS_TAG_OBJECT)
        return JS_EXCEPTION;
    p = JS_VALUE_GET_OBJ(sf->cur_func);
    if (!js_class_has_bytecode(p->class_id))
        return JS_EXCEPTION;
    b = p->u.func.function_bytecode;
    var_refs = p->u.func.var_refs;

    fd = js_new_function_def(ctx, NULL, TRUE, FALSE, filename, 1);
    if (!fd)
        return JS_EXCEPTION;
    s->cur_func = fd;
    fd->eval_type = JS_EVAL_TYPE_DIRECT;
    fd->new_target_allowed = b->new_target_allowed;
    fd->super_call_allowed = b->super_call_allowed;
    fd->super_allowed = b->super_allowed;
    fd->arguments_allowed = b->arguments_allowed;
    fd->js_mode = b->js_mode;
    fd->func_name = JS_DupAtom(ctx, JS_ATOM__eval_);
    if (add_closure_variables(ctx, fd, b, b->var_count ? DEBUG_SCOPE_INDEX : -1))
        goto fail;
    s->allow_html_comments = TRUE;
    push_scope(s);
    if (js_parse_program(s)) {
        free_token(s, &s->token);
        goto fail;
    }

    fun_obj = js_create_function(ctx, fd);
    if (JS_IsException(fun_obj))
        return fun_obj;
    if (flags & JS_EVAL_FLAG_COMPILE_ONLY)
        return fun_obj;
    return JS_EvalFunctionInternal(ctx, fun_obj, JS_UNDEFINED, var_refs, sf);

fail:
    js_free_function_def(ctx, fd);
    return JS_EXCEPTION;
}

JSValue js_debugger_evaluate(JSContext *ctx, int stack_index, JSValue expression)
{
    JSStackFrame *sf;
    int current = 0;

    for (sf = ctx->rt->current_stack_frame; sf; sf = sf->prev_frame) {
        const char *text;
        size_t length;
        JSValue result;

        if (current++ < stack_index)
            continue;
        text = JS_ToCStringLen(ctx, &length, expression);
        if (!text)
            return JS_EXCEPTION;
        result = js_debugger_eval(ctx, sf, text, length, "<debugger>", JS_EVAL_TYPE_DIRECT);
        JS_FreeCString(ctx, text);
        return result;
    }
    return JS_UNDEFINED;
}
