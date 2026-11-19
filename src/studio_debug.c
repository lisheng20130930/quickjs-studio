#include "studio_internal.h"

#include "cJSON.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

static void send_breakpoints_for_path(const char *path)
{
    int lines[512];
    int count = 0;
    for (int i = 0; i < studio.breakpoint_count; i++) {
        if (_stricmp(studio.breakpoints[i].path, path) == 0)
            lines[count++] = studio.breakpoints[i].line;
    }
    debug_session_send_breakpoints(&studio.debug, path, lines, count);
}

void studio_toggle_breakpoint(int line)
{
    const char *path = editor_path();
    studio_sync_current_breakpoints();
    if (!path[0])
        return;
    for (int i = 0; i < studio.breakpoint_count; i++) {
        if (studio.breakpoints[i].line == line + 1 &&
            _stricmp(studio.breakpoints[i].path, path) == 0) {
            memmove(&studio.breakpoints[i], &studio.breakpoints[i + 1],
                    (size_t)(studio.breakpoint_count - i - 1) * sizeof(studio.breakpoints[0]));
            studio.breakpoint_count--;
            editor_toggle_breakpoint(studio.editor, line);
            if (debug_session_is_running(&studio.debug))
                send_breakpoints_for_path(path);
            return;
        }
    }
    if (studio.breakpoint_count < (int)ARRAYSIZE(studio.breakpoints)) {
        snprintf(studio.breakpoints[studio.breakpoint_count].path,
                 sizeof(studio.breakpoints[studio.breakpoint_count].path), "%s", path);
        studio.breakpoints[studio.breakpoint_count].line = line + 1;
        studio.breakpoint_count++;
        editor_toggle_breakpoint(studio.editor, line);
        if (debug_session_is_running(&studio.debug))
            send_breakpoints_for_path(path);
    }
}

void studio_send_all_breakpoints(void)
{
    studio_sync_current_breakpoints();
    int sent[512] = {0};
    for (int i = 0; i < studio.breakpoint_count; i++) {
        if (sent[i])
            continue;
        for (int j = i; j < studio.breakpoint_count; j++) {
            if (_stricmp(studio.breakpoints[i].path, studio.breakpoints[j].path) == 0) {
                sent[j] = 1;
            }
        }
        send_breakpoints_for_path(studio.breakpoints[i].path);
    }
}

void studio_start_or_continue(void)
{
    if (studio.paused) {
        studio.paused = 0;
        editor_clear_execution_line(studio.editor);
        studio_set_watch_values("<running>");
        debug_session_send_command(&studio.debug, "continue", -1, NULL, -1);
        studio_set_status("Running");
        return;
    }
    if (debug_session_is_running(&studio.debug))
        return;
    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    if (editor_is_modified(studio.editor) && !editor_save(studio.editor)) {
        studio_show_error(studio.window, "Cannot save the current script.");
        return;
    }
    ListView_DeleteAllItems(studio.callstack);
    ListView_DeleteAllItems(studio.locals);
    studio_set_watch_values("<running>");
    editor_clear_execution_line(studio.editor);
    studio_output_append("\r\nStarting: %s %s\r\n", studio.project.executable,
                         studio.project.arguments);
    if (!debug_session_start(&studio.debug, studio.window, studio.project.executable,
                             studio.project.arguments, studio.project.working_directory)) {
        studio_show_error(studio.window, "Cannot start the debug target.");
        return;
    }
    studio.debug_connected = 0;
    SetTimer(studio.window, 1, 10000, NULL);
    studio_set_status("Starting debugger...");
}

void studio_stop_debugging(void)
{
    KillTimer(studio.window, 1);
    debug_session_stop(&studio.debug);
    studio.paused = 0;
    studio.debug_connected = 0;
    editor_clear_execution_line(studio.editor);
    studio_set_watch_values("<not available>");
    studio_set_status("Debugging stopped");
    studio_output_append("Debugging stopped.\r\n");
}

void studio_step_command(const char *command)
{
    if (!studio.paused)
        return;
    studio.paused = 0;
    editor_clear_execution_line(studio.editor);
    studio_set_watch_values("<running>");
    debug_session_send_command(&studio.debug, command, -1, NULL, -1);
    studio_set_status("Running");
}

static void evaluate_watch_row(int row)
{
    if (!studio.paused || row < 0 || row >= studio.watch_count)
        return;
    ListView_SetItemText(studio.watch_list, row, 1, "<evaluating>");
    ListView_SetItemText(studio.watch_list, row, 2, "");
    studio.watches[row].request = debug_session_send_command(&studio.debug, "evaluate", 0,
                                                             studio.watches[row].expression, -1);
}

void studio_evaluate_watches(void)
{
    for (int row = 0; row < studio.watch_count; row++)
        evaluate_watch_row(row);
}

int studio_add_watch(const char *text)
{
    char expression[256];
    char *start;
    char *end;

    snprintf(expression, sizeof(expression), "%s", text ? text : "");
    start = expression;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    if (!start[0])
        return -1;
    for (int row = 0; row < studio.watch_count; row++) {
        if (strcmp(studio.watches[row].expression, start) == 0) {
            ListView_SetItemState(studio.watch_list, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            evaluate_watch_row(row);
            return row;
        }
    }
    if (studio.watch_count >= (int)ARRAYSIZE(studio.watches)) {
        studio_show_error(studio.window, "The Watch list is full.");
        return -1;
    }
    snprintf(studio.watches[studio.watch_count].expression,
             sizeof(studio.watches[studio.watch_count].expression), "%s", start);
    studio_list_add_row(studio.watch_list, studio.watch_count, start,
                        studio.paused ? "<evaluating>" : "<not available>", "");
    studio.watch_count++;
    evaluate_watch_row(studio.watch_count - 1);
    return studio.watch_count - 1;
}

void studio_add_editor_selection_to_watch(void)
{
    char expression[256];

    if (!editor_selected_text(studio.editor, expression, sizeof(expression)))
        return;
    if (studio_add_watch(expression) < 0)
        return;
    studio_open_bottom_page(PAGE_WATCH);
    SetFocus(studio.editor);
}

void studio_delete_selected_watch(void)
{
    int row = ListView_GetNextItem(studio.watch_list, -1, LVNI_SELECTED);
    if (row < 0 || row >= studio.watch_count)
        return;
    ListView_DeleteItem(studio.watch_list, row);
    memmove(&studio.watches[row], &studio.watches[row + 1],
            (size_t)(studio.watch_count - row - 1) * sizeof(studio.watches[0]));
    studio.watch_count--;
}

static void display_stack(const cJSON *body)
{
    const cJSON *frame;
    int row = 0;
    ListView_DeleteAllItems(studio.callstack);
    cJSON_ArrayForEach(frame, body)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(frame, "name");
        const cJSON *file = cJSON_GetObjectItemCaseSensitive(frame, "filename");
        const cJSON *line = cJSON_GetObjectItemCaseSensitive(frame, "line");
        char line_text[32] = "";
        if (cJSON_IsNumber(line))
            snprintf(line_text, sizeof(line_text), "%d", line->valueint);
        studio_list_add_row(studio.callstack, row++,
                            cJSON_IsString(name) ? name->valuestring : "<anonymous>",
                            cJSON_IsString(file) ? file->valuestring : "", line_text);
        if (row == 1 && cJSON_IsString(file) && cJSON_IsNumber(line)) {
            if (_stricmp(editor_path(), file->valuestring) != 0 &&
                GetFileAttributesA(file->valuestring) != INVALID_FILE_ATTRIBUTES)
                studio_open_script(file->valuestring);
            editor_set_execution_line(studio.editor, line->valueint > 0 ? line->valueint - 1 : 0);
        }
    }
    studio.locals_request = debug_session_send_command(&studio.debug, "variables", -1, NULL, 1);
}

static void display_variables(const cJSON *body)
{
    const cJSON *variable;
    int row = 0;
    ListView_DeleteAllItems(studio.locals);
    cJSON_ArrayForEach(variable, body)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(variable, "name");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(variable, "value");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(variable, "type");
        studio_list_add_row(studio.locals, row++, cJSON_IsString(name) ? name->valuestring : "",
                            cJSON_IsString(value) ? value->valuestring : "",
                            cJSON_IsString(type) ? type->valuestring : "");
    }
}

static void display_watch_result(int row, const cJSON *body)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(body, "result");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(body, "type");
    if (row < 0 || row >= studio.watch_count)
        return;
    studio.watches[row].request = 0;
    ListView_SetItemText(studio.watch_list, row, 1,
                         cJSON_IsString(value) ? value->valuestring : "");
    ListView_SetItemText(studio.watch_list, row, 2, cJSON_IsString(type) ? type->valuestring : "");
}

void studio_process_debug_message(char *text)
{
    cJSON *root = cJSON_Parse(text);
    const cJSON *type;
    if (!root)
        goto done;
    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "event") == 0) {
        const cJSON *event = cJSON_GetObjectItemCaseSensitive(root, "event");
        const cJSON *event_type = cJSON_GetObjectItemCaseSensitive(event, "type");
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(event, "reason");
        if (cJSON_IsString(event_type) && strcmp(event_type->valuestring, "StoppedEvent") == 0) {
            if (cJSON_IsString(reason) && strcmp(reason->valuestring, "entry") == 0) {
                debug_session_send_command(&studio.debug, "continue", -1, NULL, -1);
                studio_set_status("Running");
            } else {
                studio.paused = 1;
                studio_set_status("Paused");
                studio_output_append("Paused: %s\r\n",
                                     cJSON_IsString(reason) ? reason->valuestring : "unknown");
                studio.stack_request =
                    debug_session_send_command(&studio.debug, "stackTrace", -1, NULL, -1);
                studio_evaluate_watches();
            }
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "response") == 0) {
        const cJSON *sequence = cJSON_GetObjectItemCaseSensitive(root, "request_seq");
        const cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "body");
        if (cJSON_IsNumber(sequence) && sequence->valueint == studio.stack_request)
            display_stack(body);
        else if (cJSON_IsNumber(sequence) && sequence->valueint == studio.locals_request)
            display_variables(body);
        else if (cJSON_IsNumber(sequence)) {
            for (int row = 0; row < studio.watch_count; row++) {
                if (sequence->valueint == studio.watches[row].request) {
                    display_watch_result(row, body);
                    break;
                }
            }
        }
    }
    cJSON_Delete(root);
done:
    debug_session_free_message(text);
}
