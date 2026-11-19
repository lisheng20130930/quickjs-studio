#include "studio_internal.h"

#include "Scintilla.h"
#include "editor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void studio_sync_current_breakpoints(void)
{
    const char *path = editor_path();
    int lines[512];
    int line_count;
    if (!path[0])
        return;
    for (int i = studio.breakpoint_count - 1; i >= 0; i--) {
        if (_stricmp(studio.breakpoints[i].path, path) == 0) {
            memmove(&studio.breakpoints[i], &studio.breakpoints[i + 1],
                    (size_t)(studio.breakpoint_count - i - 1) * sizeof(studio.breakpoints[0]));
            studio.breakpoint_count--;
        }
    }
    line_count = editor_collect_breakpoints(studio.editor, lines, ARRAYSIZE(lines));
    for (int i = 0; i < line_count && studio.breakpoint_count < (int)ARRAYSIZE(studio.breakpoints);
         i++) {
        snprintf(studio.breakpoints[studio.breakpoint_count].path,
                 sizeof(studio.breakpoints[studio.breakpoint_count].path), "%s", path);
        studio.breakpoints[studio.breakpoint_count].line = lines[i];
        studio.breakpoint_count++;
    }
}

void studio_refresh_navigation_files(void)
{
    int count = symbol_list_files(studio.project.script_root, studio.navigation_files,
                                  (int)ARRAYSIZE(studio.navigation_files));

    studio.navigation_file_count = 0;
    for (int index = 0; index < count; index++) {
        if (project_is_excluded(&studio.project, studio.navigation_files[index].path))
            continue;
        if (studio.navigation_file_count != index)
            studio.navigation_files[studio.navigation_file_count] = studio.navigation_files[index];
        studio.navigation_file_count++;
    }
    if (studio.project.host_api[0] &&
        GetFileAttributesA(studio.project.host_api) != INVALID_FILE_ATTRIBUTES &&
        studio.navigation_file_count < (int)ARRAYSIZE(studio.navigation_files)) {
        int duplicate = 0;
        for (int index = 0; index < studio.navigation_file_count; index++) {
            if (_stricmp(studio.navigation_files[index].path, studio.project.host_api) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            snprintf(studio.navigation_files[studio.navigation_file_count].path, MAX_PATH, "%s",
                     studio.project.host_api);
            studio.navigation_file_count++;
        }
    }
}

static void wizard_definition_text(int index, char *text, size_t capacity)
{
    FILE *file;
    char line[2048];
    int current_line = 0;
    char *start;
    char *end;

    if (!text || capacity == 0)
        return;
    text[0] = '\0';
    if (index < 0 || index >= studio.wizard_symbol_count)
        return;
    file = fopen(studio.wizard_symbols[index].path, "rb");
    if (!file)
        return;
    while (fgets(line, sizeof(line), file)) {
        current_line++;
        if (current_line == studio.wizard_symbols[index].definition.line)
            break;
    }
    fclose(file);
    if (current_line != studio.wizard_symbols[index].definition.line)
        return;
    start = line;
    while (*start == ' ' || *start == '\t')
        start++;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    snprintf(text, capacity, "%s", start);
}

static int wizard_find_symbol(const char *name)
{
    int fallback = -1;

    if (!name || !name[0])
        return -1;
    for (int index = 0; index < studio.wizard_symbol_count; index++) {
        if (strcmp(studio.wizard_symbols[index].definition.name, name) != 0)
            continue;
        if (_stricmp(studio.wizard_symbols[index].path, editor_path()) == 0)
            return index;
        if (fallback < 0)
            fallback = index;
    }
    return fallback;
}

static void wizard_set_target(int target, const char *context)
{
    int selected_item = -1;

    studio.wizard_target_index = target;
    SetWindowTextA(studio.wizard_scope, context ? context : "");
    SendMessageA(studio.wizard_member, CB_RESETCONTENT, 0, 0);
    if (target < 0 || target >= studio.wizard_symbol_count)
        return;
    for (int index = 0; index < studio.wizard_symbol_count; index++) {
        char definition_text[2048];
        LRESULT item;

        if (strcmp(studio.wizard_symbols[index].definition.name,
                   studio.wizard_symbols[target].definition.name) != 0)
            continue;
        wizard_definition_text(index, definition_text, sizeof(definition_text));
        if (!definition_text[0])
            snprintf(definition_text, sizeof(definition_text), "%s %s",
                     symbol_kind_name(studio.wizard_symbols[index].definition.kind),
                     studio.wizard_symbols[index].definition.name);
        item = SendMessageA(studio.wizard_member, CB_ADDSTRING, 0, (LPARAM)definition_text);
        if (item >= 0) {
            SendMessageA(studio.wizard_member, CB_SETITEMDATA, (WPARAM)item, index);
            if (index == target)
                selected_item = (int)item;
        }
    }
    if (selected_item >= 0)
        SendMessageA(studio.wizard_member, CB_SETCURSEL, selected_item, 0);
}

static void refresh_wizard_context_list(void)
{
    SendMessageA(studio.wizard_scope, CB_RESETCONTENT, 0, 0);
    for (int index = 0; index < studio.wizard_symbol_count; index++) {
        SymbolDefinition *definition = &studio.wizard_symbols[index].definition;
        char display[272];
        LRESULT item;

        if (_stricmp(studio.wizard_symbols[index].path, editor_path()) != 0)
            continue;
        if (definition->owner[0])
            snprintf(display, sizeof(display), "%s.%s", definition->owner, definition->name);
        else
            snprintf(display, sizeof(display), "%s", definition->name);
        item = SendMessageA(studio.wizard_scope, CB_ADDSTRING, 0, (LPARAM)display);
        if (item >= 0)
            SendMessageA(studio.wizard_scope, CB_SETITEMDATA, (WPARAM)item, index);
    }
}

void studio_refresh_navigation_symbols(void)
{
    SendMessageA(studio.wizard_scope, CB_RESETCONTENT, 0, 0);
    studio.wizard_symbol_count = 0;
    for (int file_index = 0; file_index < studio.navigation_file_count; file_index++) {
        SymbolDefinition definitions[512];
        int count = symbol_list_file_definitions(studio.navigation_files[file_index].path,
                                                 definitions, (int)ARRAYSIZE(definitions));
        for (int index = 0;
             index < count && studio.wizard_symbol_count < (int)ARRAYSIZE(studio.wizard_symbols);
             index++) {
            WizardSymbol *symbol = &studio.wizard_symbols[studio.wizard_symbol_count++];
            symbol->definition = definitions[index];
            snprintf(symbol->path, sizeof(symbol->path), "%s",
                     studio.navigation_files[file_index].path);
        }
    }
    studio.wizard_target_index = -1;
    refresh_wizard_context_list();
    studio_sync_wizard_to_caret();
}

void studio_sync_wizard_to_caret(void)
{
    char symbol[128];
    int line = editor_current_line(studio.editor) + 1;
    int target = -1;
    int best_line = -1;

    if (editor_symbol_at_caret(studio.editor, symbol, sizeof(symbol))) {
        target = wizard_find_symbol(symbol);
        wizard_set_target(target, symbol);
        return;
    }
    for (int index = 0; index < studio.wizard_symbol_count; index++) {
        SymbolDefinition *definition = &studio.wizard_symbols[index].definition;
        if (_stricmp(studio.wizard_symbols[index].path, editor_path()) != 0)
            continue;
        if ((definition->kind == SYMBOL_FUNCTION || definition->kind == SYMBOL_METHOD ||
             definition->kind == SYMBOL_CLASS) &&
            definition->line <= line && definition->line > best_line) {
            best_line = definition->line;
            target = index;
        }
    }
    wizard_set_target(target,
                      target >= 0 ? studio.wizard_symbols[target].definition.name : editor_path());
}

int studio_open_script(const char *path)
{
    if (_stricmp(editor_path(), path) == 0)
        return 1;
    if (editor_is_modified(studio.editor)) {
        int answer = MessageBoxA(studio.window, "Save the current script?", "QuickJS Studio",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !editor_save(studio.editor)))
            return 0;
    }
    studio_sync_current_breakpoints();
    if (!editor_open(studio.editor, path)) {
        studio_show_error(studio.window, "Cannot open the JavaScript file.");
        return 0;
    }
    ShowWindow(studio.document_background, SW_HIDE);
    ShowWindow(studio.editor, SW_SHOW);
    studio_set_document_close_button(1);
    studio_layout_controls(studio.window);
    studio_update_editor_title(path);
    for (int i = 0; i < studio.breakpoint_count; i++) {
        if (_stricmp(studio.breakpoints[i].path, path) == 0)
            editor_toggle_breakpoint(studio.editor, studio.breakpoints[i].line - 1);
    }
    studio_refresh_navigation_files();
    studio_refresh_navigation_symbols();
    studio_sync_wizard_to_caret();
    return 1;
}

int studio_go_to_navigation_symbol(void)
{
    int definition_index = studio.wizard_target_index;
    int line;
    char path[MAX_PATH];

    if (definition_index < 0) {
        int selected = (int)SendMessageA(studio.wizard_member, CB_GETCURSEL, 0, 0);
        if (selected >= 0)
            definition_index =
                (int)SendMessageA(studio.wizard_member, CB_GETITEMDATA, (WPARAM)selected, 0);
    }
    if (definition_index < 0 || definition_index >= studio.wizard_symbol_count)
        return 0;
    snprintf(path, sizeof(path), "%s", studio.wizard_symbols[definition_index].path);
    line = studio.wizard_symbols[definition_index].definition.line;
    if (studio_open_script(path)) {
        editor_goto_line(studio.editor, line - 1);
        return 1;
    }
    return 0;
}

static int go_to_selected_definition(void)
{
    char symbol[128];
    char path[MAX_PATH];
    int line;
    SymbolDefinition definitions[512];
    int definition_count;

    if (!editor_symbol_at_caret(studio.editor, symbol, sizeof(symbol)))
        return 0;
    definition_count =
        symbol_list_file_definitions(editor_path(), definitions, (int)ARRAYSIZE(definitions));
    for (int index = 0; index < definition_count; index++) {
        if (strcmp(definitions[index].name, symbol) == 0) {
            editor_goto_line(studio.editor, definitions[index].line - 1);
            return 1;
        }
    }
    path[0] = '\0';
    line = 0;
    for (int file = 0; file < studio.navigation_file_count && !path[0]; file++) {
        if (_stricmp(studio.navigation_files[file].path, editor_path()) == 0)
            continue;
        definition_count = symbol_list_file_definitions(studio.navigation_files[file].path,
                                                        definitions, (int)ARRAYSIZE(definitions));
        for (int index = 0; index < definition_count; index++) {
            if (strcmp(definitions[index].name, symbol) == 0) {
                snprintf(path, sizeof(path), "%s", studio.navigation_files[file].path);
                line = definitions[index].line;
                break;
            }
        }
    }
    if (!path[0]) {
        return 0;
    }
    if (studio_open_script(path)) {
        editor_goto_line(studio.editor, line - 1);
        return 1;
    }
    return 0;
}

void studio_go_to_definition(void)
{
    if (!go_to_selected_definition() && !studio_go_to_navigation_symbol())
        studio_set_status("Definition not found");
}

#define MAX_COMPLETION_WORDS 2048
#define MAX_COMPLETION_LENGTH 128

typedef struct CompletionWord {
    char text[MAX_COMPLETION_LENGTH];
} CompletionWord;

static int completion_compare(const void *left, const void *right)
{
    const CompletionWord *a = (const CompletionWord *)left;
    const CompletionWord *b = (const CompletionWord *)right;
    return _stricmp(a->text, b->text);
}

static int completion_identifier_start(int character)
{
    return isalpha((unsigned char)character) || character == '_' || character == '$';
}

int studio_is_completion_character(int character)
{
    return isalnum((unsigned char)character) || character == '_' || character == '$';
}

static void completion_add(CompletionWord *words, int *count, const char *text, size_t length)
{
    if (!text || length == 0 || length >= MAX_COMPLETION_LENGTH || *count >= MAX_COMPLETION_WORDS ||
        !completion_identifier_start((unsigned char)text[0]))
        return;
    for (size_t index = 1; index < length; index++) {
        if (!studio_is_completion_character((unsigned char)text[index]))
            return;
    }
    for (int index = 0; index < *count; index++) {
        if (strlen(words[index].text) == length && _strnicmp(words[index].text, text, length) == 0)
            return;
    }
    memcpy(words[*count].text, text, length);
    words[*count].text[length] = '\0';
    (*count)++;
}

static void completion_add_tokens(CompletionWord *words, int *count, const char *text)
{
    const char *cursor = text;

    while (cursor && *cursor) {
        const char *start;
        while (*cursor && !completion_identifier_start((unsigned char)*cursor))
            cursor++;
        start = cursor;
        while (*cursor && studio_is_completion_character((unsigned char)*cursor))
            cursor++;
        completion_add(words, count, start, (size_t)(cursor - start));
    }
}

static int completion_owner_before_caret(LRESULT position, char *owner, size_t capacity)
{
    LRESULT word_start;
    LRESULT dot;
    LRESULT start;
    size_t length;

    if (!owner || capacity == 0)
        return 0;
    owner[0] = '\0';
    word_start = SendMessageA(studio.editor, SCI_WORDSTARTPOSITION, (WPARAM)position, TRUE);
    dot = word_start > 0 && SendMessageA(studio.editor, SCI_GETCHARAT, word_start - 1, 0) == '.'
              ? word_start - 1
          : position > 0 && SendMessageA(studio.editor, SCI_GETCHARAT, position - 1, 0) == '.'
              ? position - 1
              : -1;
    if (dot < 1)
        return 0;
    start = dot;
    while (start > 0 && studio_is_completion_character(
                            (int)SendMessageA(studio.editor, SCI_GETCHARAT, start - 1, 0)))
        start--;
    length = (size_t)(dot - start);
    if (!length || length >= capacity)
        return 0;
    for (size_t index = 0; index < length; index++)
        owner[index] = (char)SendMessageA(studio.editor, SCI_GETCHARAT, start + (LRESULT)index, 0);
    owner[length] = '\0';
    return 1;
}

void studio_show_autocomplete(int forced)
{
    static const char javascript_keywords[] =
        "as async await break case catch class const continue debugger default delete do else "
        "export extends false finally for from function get if import in instanceof let new null "
        "of return set static super switch this throw true try typeof undefined var void while "
        "with yield";
    static const char javascript_globals[] =
        "Array ArrayBuffer Atomics BigInt BigInt64Array BigUint64Array Boolean DataView Date Error "
        "EvalError FinalizationRegistry Float32Array Float64Array Function Infinity Int8Array "
        "Int16Array Int32Array Intl JSON Map Math NaN Number Object Promise Proxy RangeError "
        "ReferenceError Reflect RegExp Set SharedArrayBuffer String Symbol SyntaxError TypeError "
        "URIError Uint8Array Uint8ClampedArray Uint16Array Uint32Array WeakMap WeakRef WeakSet "
        "WebAssembly console globalThis decodeURI decodeURIComponent encodeURI "
        "encodeURIComponent eval isFinite isNaN parseFloat parseInt";
    static const char javascript_members[] =
        "add all allSettled any assert assign catch charAt clear concat constructor debug delete "
        "dir endsWith entries error every exec filter finally find findIndex flat flatMap forEach "
        "freeze from get has includes indexOf info is isArray join keys lastIndexOf length log map "
        "match matchAll message name of pop prototype push race reduce reduceRight reject replace "
        "replaceAll resolve reverse search seal set shift size slice some sort splice split "
        "startsWith stack substring substr test then time timeEnd toLowerCase toString toUpperCase "
        "trim trimEnd trimStart unshift valueOf values warn";
    CompletionWord *words;
    char *document = NULL;
    char *list = NULL;
    LRESULT position;
    LRESULT word_start;
    LRESULT document_length;
    int prefix_length;
    int word_count = 0;
    int owner_member_count = 0;
    char owner[128];
    size_t list_capacity;
    size_t list_length = 0;

    if (!studio.editor || (!forced && SendMessageA(studio.editor, SCI_AUTOCACTIVE, 0, 0)))
        return;
    position = SendMessageA(studio.editor, SCI_GETCURRENTPOS, 0, 0);
    word_start = SendMessageA(studio.editor, SCI_WORDSTARTPOSITION, (WPARAM)position, TRUE);
    prefix_length = (int)(position - word_start);
    if (!forced && prefix_length < 2)
        return;

    words = (CompletionWord *)calloc(MAX_COMPLETION_WORDS, sizeof(*words));
    if (!words)
        return;
    if (completion_owner_before_caret(position, owner, sizeof(owner))) {
        for (int index = 0; index < studio.wizard_symbol_count; index++) {
            SymbolDefinition *definition = &studio.wizard_symbols[index].definition;
            if (strcmp(definition->owner, owner) != 0)
                continue;
            completion_add(words, &word_count, definition->name, strlen(definition->name));
        }
        owner_member_count = word_count;
    }
    if (!owner_member_count) {
        completion_add_tokens(words, &word_count, javascript_keywords);
        completion_add_tokens(words, &word_count, javascript_globals);
        completion_add_tokens(words, &word_count, javascript_members);
        for (int index = 0; index < studio.wizard_symbol_count; index++) {
            const char *name = studio.wizard_symbols[index].definition.name;
            completion_add(words, &word_count, name, strlen(name));
        }
    }

    document_length = SendMessageA(studio.editor, SCI_GETLENGTH, 0, 0);
    if (!owner_member_count && document_length > 0) {
        document = (char *)malloc((size_t)document_length + 1);
        if (document) {
            SendMessageA(studio.editor, SCI_GETTEXT, (WPARAM)document_length + 1, (LPARAM)document);
            completion_add_tokens(words, &word_count, document);
        }
    }
    free(document);
    if (word_count == 0) {
        free(words);
        return;
    }

    qsort(words, (size_t)word_count, sizeof(*words), completion_compare);
    list_capacity = (size_t)word_count * MAX_COMPLETION_LENGTH + 1;
    list = (char *)malloc(list_capacity);
    if (!list) {
        free(words);
        return;
    }
    list[0] = '\0';
    for (int index = 0; index < word_count; index++) {
        size_t length = strlen(words[index].text);
        if (index > 0)
            list[list_length++] = ' ';
        memcpy(list + list_length, words[index].text, length);
        list_length += length;
        list[list_length] = '\0';
    }

    SendMessageA(studio.editor, SCI_AUTOCSETSEPARATOR, ' ', 0);
    SendMessageA(studio.editor, SCI_AUTOCSETIGNORECASE, TRUE, 0);
    SendMessageA(studio.editor, SCI_AUTOCSETDROPRESTOFWORD, TRUE, 0);
    SendMessageA(studio.editor, SCI_AUTOCSETCHOOSESINGLE, FALSE, 0);
    SendMessageA(studio.editor, SCI_AUTOCSETMAXHEIGHT, 12, 0);
    SendMessageA(studio.editor, SCI_AUTOCSETMAXWIDTH, 40, 0);
    SendMessageA(studio.editor, SCI_AUTOCSETORDER, SC_ORDER_PRESORTED, 0);
    SendMessageA(studio.editor, SCI_AUTOCSHOW, (WPARAM)prefix_length, (LPARAM)list);
    free(list);
    free(words);
}

static TreeTarget *copy_tree_target(const char *path, int line, int is_directory, int is_physical)
{
    TreeTarget *target = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*target));
    if (target) {
        snprintf(target->path, sizeof(target->path), "%s", path);
        target->line = line;
        target->is_directory = is_directory;
        target->is_physical = is_physical;
    }
    return target;
}

static void populate_directory(HTREEITEM parent, const char *directory)
{
    char pattern[MAX_PATH];

    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    for (int directories = 1; directories >= 0; directories--) {
        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA(pattern, &data);

        if (find == INVALID_HANDLE_VALUE)
            return;
        do {
            char path[MAX_PATH];
            TVINSERTSTRUCTA insert = {0};
            int is_directory;
            size_t name_length;

            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
                continue;
            is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (is_directory != directories)
                continue;
            snprintf(path, sizeof(path), "%s\\%s", directory, data.cFileName);
            if (project_is_excluded(&studio.project, path))
                continue;
            insert.hParent = parent;
            insert.hInsertAfter = TVI_LAST;
            insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            insert.item.pszText = data.cFileName;
            insert.item.iImage = is_directory ? TREE_IMAGE_FOLDER : TREE_IMAGE_FILE;
            insert.item.iSelectedImage = insert.item.iImage;
            if (is_directory) {
                insert.item.mask |= TVIF_PARAM;
                insert.item.lParam = (LPARAM)copy_tree_target(path, 0, 1, 1);
                HTREEITEM branch = TreeView_InsertItem(studio.tree, &insert);
                populate_directory(branch, path);
                continue;
            }
            name_length = strlen(data.cFileName);
            if (name_length < 3 || _stricmp(data.cFileName + name_length - 3, ".js") != 0)
                continue;
            insert.item.mask |= TVIF_PARAM;
            insert.item.lParam = (LPARAM)copy_tree_target(path, 0, 0, 1);
            TreeView_InsertItem(studio.tree, &insert);
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
}

void studio_populate_project_tree(void)
{
    TVINSERTSTRUCTA insert = {0};
    HTREEITEM root;
    HTREEITEM project;
    HTREEITEM source;
    char workspace_title[256];
    char project_title[192];

    TreeView_DeleteAllItems(studio.tree);
    insert.hParent = TVI_ROOT;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
    insert.item.iImage = TREE_IMAGE_WORKSPACE;
    insert.item.iSelectedImage = TREE_IMAGE_WORKSPACE;
    insert.item.lParam = (LPARAM)copy_tree_target(studio.project.script_root, 0, 1, 0);
    snprintf(workspace_title, sizeof(workspace_title), "Workspace '%s': 1 project(s)",
             studio.project.name);
    insert.item.pszText = workspace_title;
    root = TreeView_InsertItem(studio.tree, &insert);
    snprintf(project_title, sizeof(project_title), "%s files", studio.project.name);
    insert.hParent = root;
    insert.item.pszText = project_title;
    insert.item.iImage = TREE_IMAGE_WORKSPACE;
    insert.item.iSelectedImage = TREE_IMAGE_WORKSPACE;
    insert.item.lParam = (LPARAM)copy_tree_target(studio.project.script_root, 0, 1, 0);
    project = TreeView_InsertItem(studio.tree, &insert);
    insert.hParent = project;
    insert.item.pszText = "Source Files";
    insert.item.iImage = TREE_IMAGE_FOLDER;
    insert.item.iSelectedImage = TREE_IMAGE_FOLDER;
    insert.item.lParam = (LPARAM)copy_tree_target(studio.project.script_root, 0, 1, 0);
    source = TreeView_InsertItem(studio.tree, &insert);
    populate_directory(source, studio.project.script_root);
    TreeView_Expand(studio.tree, root, TVE_EXPAND);
    TreeView_Expand(studio.tree, project, TVE_EXPAND);
    TreeView_Expand(studio.tree, source, TVE_EXPAND);
}

void studio_populate_class_tree(void)
{
    TVINSERTSTRUCTA insert = {0};
    HTREEITEM root;
    HTREEITEM globals;
    char title[256];

    TreeView_DeleteAllItems(studio.tree);
    insert.hParent = TVI_ROOT;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.iImage = TREE_IMAGE_WORKSPACE;
    insert.item.iSelectedImage = TREE_IMAGE_WORKSPACE;
    snprintf(title, sizeof(title), "%s classes", studio.project.name);
    insert.item.pszText = title;
    root = TreeView_InsertItem(studio.tree, &insert);
    for (int file_index = 0; file_index < studio.navigation_file_count; file_index++) {
        SymbolDefinition definitions[256];
        int count = symbol_list_file_definitions(studio.navigation_files[file_index].path,
                                                 definitions, (int)ARRAYSIZE(definitions));
        for (int class_index = 0; class_index < count; class_index++) {
            HTREEITEM class_item;
            if (definitions[class_index].kind != SYMBOL_CLASS)
                continue;
            insert.hParent = root;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            insert.item.pszText = definitions[class_index].name;
            insert.item.iImage = TREE_IMAGE_CLASS;
            insert.item.iSelectedImage = TREE_IMAGE_CLASS;
            insert.item.lParam = (LPARAM)copy_tree_target(studio.navigation_files[file_index].path,
                                                          definitions[class_index].line, 0, 0);
            class_item = TreeView_InsertItem(studio.tree, &insert);
            for (int member_index = 0; member_index < count; member_index++) {
                if (definitions[member_index].kind != SYMBOL_METHOD ||
                    strcmp(definitions[member_index].owner, definitions[class_index].name) != 0)
                    continue;
                insert.hParent = class_item;
                insert.item.pszText = definitions[member_index].name;
                insert.item.iImage = TREE_IMAGE_FUNCTION;
                insert.item.iSelectedImage = TREE_IMAGE_FUNCTION;
                insert.item.lParam = (LPARAM)copy_tree_target(
                    studio.navigation_files[file_index].path, definitions[member_index].line, 0, 0);
                TreeView_InsertItem(studio.tree, &insert);
            }
        }
    }
    insert.hParent = root;
    insert.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.pszText = "Globals";
    insert.item.iImage = TREE_IMAGE_FOLDER;
    insert.item.iSelectedImage = TREE_IMAGE_FOLDER;
    globals = TreeView_InsertItem(studio.tree, &insert);
    for (int file_index = 0; file_index < studio.navigation_file_count; file_index++) {
        SymbolDefinition definitions[256];
        int count = symbol_list_file_definitions(studio.navigation_files[file_index].path,
                                                 definitions, (int)ARRAYSIZE(definitions));
        for (int symbol_index = 0; symbol_index < count; symbol_index++) {
            if (definitions[symbol_index].owner[0] ||
                definitions[symbol_index].kind == SYMBOL_CLASS)
                continue;
            insert.hParent = globals;
            insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            insert.item.pszText = definitions[symbol_index].name;
            insert.item.iImage = definitions[symbol_index].kind == SYMBOL_VARIABLE
                                     ? TREE_IMAGE_VARIABLE
                                     : TREE_IMAGE_FUNCTION;
            insert.item.iSelectedImage = insert.item.iImage;
            insert.item.lParam = (LPARAM)copy_tree_target(studio.navigation_files[file_index].path,
                                                          definitions[symbol_index].line, 0, 0);
            TreeView_InsertItem(studio.tree, &insert);
        }
    }
    TreeView_Expand(studio.tree, root, TVE_EXPAND);
    TreeView_Expand(studio.tree, globals, TVE_EXPAND);
}
