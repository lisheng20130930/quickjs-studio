#include "symbol_search.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static SymbolDefinition *find_definition(SymbolDefinition *definitions, int count, const char *name)
{
    for (int index = 0; index < count; index++) {
        if (strcmp(definitions[index].name, name) == 0)
            return &definitions[index];
    }
    return NULL;
}

int main(void)
{
    const char *fixture =
#ifdef _WIN32
        "tests\\fixtures\\wizard_symbols.js";
#else
        "tests/fixtures/wizard_symbols.js";
#endif
    SymbolDefinition definitions[32];
    SymbolDefinition *definition;
    char path[MAX_PATH];
    int line;
    int count = symbol_list_file_definitions(fixture, definitions, 32);

    assert(count == 6);
    definition = find_definition(definitions, count, "Player");
    assert(definition && definition->kind == SYMBOL_CLASS && definition->end_line == 9);
    definition = find_definition(definitions, count, "constructor");
    assert(definition && definition->kind == SYMBOL_METHOD &&
           strcmp(definition->owner, "Player") == 0);
    definition = find_definition(definitions, count, "move");
    assert(definition && definition->kind == SYMBOL_METHOD && definition->line == 6);
    definition = find_definition(definitions, count, "tick");
    assert(definition && definition->kind == SYMBOL_FUNCTION);
    definition = find_definition(definitions, count, "calculate");
    assert(definition && definition->kind == SYMBOL_FUNCTION);
    definition = find_definition(definitions, count, "gravity");
    assert(definition && definition->kind == SYMBOL_VARIABLE);
    assert(!find_definition(definitions, count, "hiddenLocal"));
    assert(symbol_find_definition("tests\\fixtures", fixture, "move", path, sizeof(path), &line));
    assert(line == 6);
    assert(symbol_find_definition("examples", "examples/game/update.js", "calculateDistance", path,
                                  sizeof(path), &line));
    assert(line == 1 && strstr(path, "distance.js"));
    count = symbol_list_file_definitions("examples/.quickjs-studio/host-api.js", definitions, 32);
    definition = find_definition(definitions, count, "Engine");
    assert(definition && definition->kind == SYMBOL_CLASS);
    definition = find_definition(definitions, count, "log");
    assert(definition && definition->kind == SYMBOL_METHOD &&
           strcmp(definition->owner, "Engine") == 0);
    puts("WizardBar symbol search: PASS");
    return 0;
}
