#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

#include "symbol_search.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *skip_space(const char *text)
{
    while (*text && isspace((unsigned char)*text))
        text++;
    return text;
}

static int take_keyword(const char **text, const char *keyword)
{
    size_t length = strlen(keyword);
    const char *current = skip_space(*text);

    if (strncmp(current, keyword, length) != 0 ||
        (isalnum((unsigned char)current[length]) || current[length] == '_' ||
         current[length] == '$'))
        return 0;
    *text = current + length;
    return 1;
}

static int take_identifier(const char **text, char *identifier, size_t capacity)
{
    const char *current = skip_space(*text);
    const char *start = current;
    size_t length;

    if (!isalpha((unsigned char)*current) && *current != '_' && *current != '$')
        return 0;
    current++;
    while (isalnum((unsigned char)*current) || *current == '_' || *current == '$')
        current++;
    length = (size_t)(current - start);
    if (length >= capacity)
        return 0;
    memcpy(identifier, start, length);
    identifier[length] = '\0';
    *text = current;
    return 1;
}

static int parse_line_definition(const char *line, char *identifier, size_t capacity,
                                 SymbolKind *kind)
{
    const char *text = skip_space(line);

    if (!*text || (text[0] == '/' && (text[1] == '/' || text[1] == '*')) || text[0] == '*')
        return 0;
    take_keyword(&text, "export");
    take_keyword(&text, "default");
    take_keyword(&text, "async");
    if (take_keyword(&text, "function")) {
        text = skip_space(text);
        if (*text == '*')
            text++;
        if (!take_identifier(&text, identifier, capacity))
            return 0;
        *kind = SYMBOL_FUNCTION;
        return 1;
    }
    if (take_keyword(&text, "class")) {
        if (!take_identifier(&text, identifier, capacity))
            return 0;
        *kind = SYMBOL_CLASS;
        return 1;
    }
    text = skip_space(line);
    take_keyword(&text, "export");
    if (take_keyword(&text, "const") || take_keyword(&text, "let") || take_keyword(&text, "var")) {
        if (!take_identifier(&text, identifier, capacity))
            return 0;
        text = skip_space(text);
        if (*text != '=')
            return 0;
        *kind =
            (strstr(text, "=>") || strstr(text, "function")) ? SYMBOL_FUNCTION : SYMBOL_VARIABLE;
        return 1;
    }
    return 0;
}

static int parse_class_method(const char *line, char *identifier, size_t capacity)
{
    const char *text = skip_space(line);
    const char *after_name;

    if (!*text || (text[0] == '/' && (text[1] == '/' || text[1] == '*')) || text[0] == '*' ||
        text[0] == '}')
        return 0;
    take_keyword(&text, "static");
    take_keyword(&text, "async");
    take_keyword(&text, "get");
    take_keyword(&text, "set");
    if (*text == '*')
        text++;
    if (!take_identifier(&text, identifier, capacity))
        return 0;
    after_name = skip_space(text);
    return *after_name == '(';
}

static int count_braces(const char *line)
{
    int braces = 0;
    int quote = 0;
    int escaped = 0;

    for (const unsigned char *text = (const unsigned char *)line; *text; text++) {
        if (quote) {
            if (escaped) {
                escaped = 0;
            } else if (*text == '\\') {
                escaped = 1;
            } else if (*text == quote) {
                quote = 0;
            }
            continue;
        }
        if (*text == '\'' || *text == '"' || *text == '`') {
            quote = *text;
            continue;
        }
        if (text[0] == '/' && text[1] == '/')
            break;
        if (*text == '{')
            braces++;
        else if (*text == '}')
            braces--;
    }
    return braces;
}

static int search_file(const char *file_path, const char *symbol, char *path, size_t path_capacity,
                       int *definition_line)
{
    SymbolDefinition definitions[512];
    int count = symbol_list_file_definitions(file_path, definitions,
                                             (int)(sizeof(definitions) / sizeof(definitions[0])));

    for (int index = 0; index < count; index++) {
        if (strcmp(definitions[index].name, symbol) == 0) {
            snprintf(path, path_capacity, "%s", file_path);
            *definition_line = definitions[index].line;
            return 1;
        }
    }
    return 0;
}

static int is_javascript_file(const char *name)
{
    size_t length = strlen(name);
#ifdef _WIN32
    return length >= 3 && _stricmp(name + length - 3, ".js") == 0;
#else
    return length >= 3 && strcasecmp(name + length - 3, ".js") == 0;
#endif
}

int symbol_list_file_definitions(const char *path, SymbolDefinition *definitions, int capacity)
{
    FILE *file;
    char text[8192];
    int line = 0;
    int count = 0;
    int brace_depth = 0;
    int class_depth = -1;
    int class_index = -1;
    char active_class[128] = "";

    if (!path || !definitions || capacity <= 0 || !(file = fopen(path, "rb")))
        return 0;
    while (fgets(text, sizeof(text), file)) {
        char identifier[128];
        SymbolKind kind;
        int brace_delta;

        line++;
        if (active_class[0] && brace_depth == class_depth + 1 && count < capacity &&
            parse_class_method(text, identifier, sizeof(identifier))) {
            snprintf(definitions[count].name, sizeof(definitions[count].name), "%s", identifier);
            snprintf(definitions[count].owner, sizeof(definitions[count].owner), "%s",
                     active_class);
            definitions[count].kind = SYMBOL_METHOD;
            definitions[count].line = line;
            definitions[count].end_line = line;
            count++;
        } else if (brace_depth == 0 && count < capacity &&
                   parse_line_definition(text, identifier, sizeof(identifier), &kind)) {
            snprintf(definitions[count].name, sizeof(definitions[count].name), "%s", identifier);
            definitions[count].owner[0] = '\0';
            definitions[count].kind = kind;
            definitions[count].line = line;
            definitions[count].end_line = line;
            if (kind == SYMBOL_CLASS) {
                snprintf(active_class, sizeof(active_class), "%s", identifier);
                class_index = count;
                class_depth = brace_depth;
            }
            count++;
        }
        brace_delta = count_braces(text);
        brace_depth += brace_delta;
        if (active_class[0] && brace_depth <= class_depth && strchr(text, '}')) {
            if (class_index >= 0)
                definitions[class_index].end_line = line;
            active_class[0] = '\0';
            class_depth = -1;
            class_index = -1;
        }
    }
    if (class_index >= 0)
        definitions[class_index].end_line = line;
    fclose(file);
    return count;
}

const char *symbol_kind_name(SymbolKind kind)
{
    switch (kind) {
    case SYMBOL_FUNCTION:
        return "Function";
    case SYMBOL_CLASS:
        return "Class";
    case SYMBOL_METHOD:
        return "Method";
    case SYMBOL_VARIABLE:
        return "Variable";
    }
    return "Symbol";
}

static void list_directory_files(const char *directory, SymbolFile *files, int capacity, int *count)
{
#ifdef _WIN32
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE find;

    if (*count >= capacity)
        return;
    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char child[MAX_PATH];
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s\\%s", directory, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            list_directory_files(child, files, capacity, count);
        } else if (*count < capacity && is_javascript_file(data.cFileName)) {
            snprintf(files[*count].path, sizeof(files[*count].path), "%s", child);
            (*count)++;
        }
    } while (*count < capacity && FindNextFileA(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(directory);
    struct dirent *entry;

    if (!dir)
        return;
    while (*count < capacity && (entry = readdir(dir)) != NULL) {
        char child[MAX_PATH];
        struct stat status;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name);
        if (stat(child, &status) != 0)
            continue;
        if (S_ISDIR(status.st_mode)) {
            list_directory_files(child, files, capacity, count);
        } else if (*count < capacity && is_javascript_file(entry->d_name)) {
            snprintf(files[*count].path, sizeof(files[*count].path), "%s", child);
            (*count)++;
        }
    }
    closedir(dir);
#endif
}

int symbol_list_files(const char *script_root, SymbolFile *files, int capacity)
{
    int count = 0;
    if (!script_root || !script_root[0] || !files || capacity <= 0)
        return 0;
    list_directory_files(script_root, files, capacity, &count);
    return count;
}

static int search_directory(const char *directory, const char *skip_file, const char *symbol,
                            char *path, size_t path_capacity, int *definition_line)
{
#ifdef _WIN32
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE find;

    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char child[MAX_PATH];
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s\\%s", directory, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (search_directory(child, skip_file, symbol, path, path_capacity, definition_line)) {
                FindClose(find);
                return 1;
            }
        } else if (is_javascript_file(data.cFileName) &&
                   (!skip_file || _stricmp(child, skip_file) != 0) &&
                   search_file(child, symbol, path, path_capacity, definition_line)) {
            FindClose(find);
            return 1;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return 0;
#else
    DIR *dir = opendir(directory);
    struct dirent *entry;

    if (!dir)
        return 0;
    while ((entry = readdir(dir)) != NULL) {
        char child[MAX_PATH];
        struct stat status;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name);
        if (stat(child, &status) != 0)
            continue;
        if (S_ISDIR(status.st_mode)) {
            if (search_directory(child, skip_file, symbol, path, path_capacity, definition_line)) {
                closedir(dir);
                return 1;
            }
        } else if (is_javascript_file(entry->d_name) &&
                   (!skip_file || strcmp(child, skip_file) != 0) &&
                   search_file(child, symbol, path, path_capacity, definition_line)) {
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
#endif
}

int symbol_find_definition(const char *script_root, const char *preferred_file, const char *symbol,
                           char *path, size_t path_capacity, int *line)
{
    if (!script_root || !script_root[0] || !symbol || !symbol[0] || !path || !path_capacity ||
        !line)
        return 0;
    if (preferred_file && preferred_file[0] &&
        search_file(preferred_file, symbol, path, path_capacity, line))
        return 1;
    return search_directory(script_root, preferred_file, symbol, path, path_capacity, line);
}
