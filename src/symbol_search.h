#ifndef QJS_STUDIO_SYMBOL_SEARCH_H
#define QJS_STUDIO_SYMBOL_SEARCH_H

#ifdef _WIN32
#include <windows.h>
#else
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#endif

#include <stddef.h>

typedef enum SymbolKind {
    SYMBOL_FUNCTION,
    SYMBOL_CLASS,
    SYMBOL_METHOD,
    SYMBOL_VARIABLE
} SymbolKind;

typedef struct SymbolDefinition {
    char name[128];
    char owner[128];
    SymbolKind kind;
    int line;
    int end_line;
} SymbolDefinition;

typedef struct SymbolFile {
    char path[MAX_PATH];
} SymbolFile;

int symbol_list_files(const char *script_root, SymbolFile *files, int capacity);
int symbol_list_file_definitions(const char *path, SymbolDefinition *definitions, int capacity);

const char *symbol_kind_name(SymbolKind kind);

int symbol_find_definition(const char *script_root, const char *preferred_file, const char *symbol,
                           char *path, size_t path_capacity, int *line);

#endif
