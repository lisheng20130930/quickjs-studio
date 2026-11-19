#include "project.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int read_raw_value(const char *path, const char *section, const char *key, char *value,
                          size_t capacity)
{
    FILE *file = fopen(path, "rb");
    char line[4096];
    int in_section = 0;
    if (!file)
        return 0;
    while (fgets(line, sizeof(line), file)) {
        char *text = line;
        char *end;
        char *equals;
        while (isspace((unsigned char)*text))
            text++;
        end = text + strlen(text);
        while (end > text && isspace((unsigned char)end[-1]))
            *--end = '\0';
        if (*text == '[' && end > text + 2 && end[-1] == ']') {
            end[-1] = '\0';
            in_section = _stricmp(text + 1, section) == 0;
            continue;
        }
        if (!in_section || *text == ';' || *text == '#' || !*text)
            continue;
        equals = strchr(text, '=');
        if (!equals)
            continue;
        end = equals;
        while (end > text && isspace((unsigned char)end[-1]))
            end--;
        if ((size_t)(end - text) != strlen(key) || _strnicmp(text, key, strlen(key)) != 0)
            continue;
        text = equals + 1;
        while (isspace((unsigned char)*text))
            text++;
        snprintf(value, capacity, "%s", text);
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

static void directory_of(const char *path, char *directory, size_t capacity)
{
    const char *slash;
    snprintf(directory, capacity, "%s", path);
    slash = strrchr(directory, '\\');
    if (!slash)
        slash = strrchr(directory, '/');
    if (slash)
        directory[slash - directory] = '\0';
    else
        snprintf(directory, capacity, ".");
}

static void absolute_from(const char *base, const char *value, char *output, size_t capacity)
{
    char combined[4096];
    if (!value[0]) {
        output[0] = '\0';
        return;
    }
    if ((strlen(value) > 2 && value[1] == ':') || value[0] == '\\' || value[0] == '/') {
        snprintf(combined, sizeof(combined), "%s", value);
    } else {
        snprintf(combined, sizeof(combined), "%s\\%s", base, value);
    }
    if (!GetFullPathNameA(combined, (DWORD)capacity, output, NULL))
        snprintf(output, capacity, "%s", combined);
}

static void replace_macro(char *text, size_t capacity, const char *macro, const char *value)
{
    char result[4096];
    const char *source = text;
    const char *found;
    size_t macro_length = strlen(macro);
    size_t value_length = strlen(value);
    size_t length = 0;

    while ((found = strstr(source, macro)) != NULL) {
        size_t prefix_length = (size_t)(found - source);
        if (length + prefix_length + value_length >= sizeof(result) ||
            length + prefix_length + value_length >= capacity)
            return;
        memcpy(result + length, source, prefix_length);
        length += prefix_length;
        memcpy(result + length, value, value_length);
        length += value_length;
        source = found + macro_length;
    }
    if (length + strlen(source) >= sizeof(result) || length + strlen(source) >= capacity)
        return;
    memcpy(result + length, source, strlen(source) + 1);
    memcpy(text, result, length + strlen(source) + 1);
}

static int path_is_same_or_child(const char *parent, const char *path)
{
    size_t length = strlen(parent);

    while (length > 0 && (parent[length - 1] == '\\' || parent[length - 1] == '/'))
        length--;
    return _strnicmp(parent, path, length) == 0 &&
           (path[length] == '\0' || path[length] == '\\' || path[length] == '/');
}

static void load_excluded_paths(StudioProject *project)
{
    char entries[32768];
    char *entry;

    if (!GetPrivateProfileSectionA("Excluded", entries, sizeof(entries), project->project_file))
        return;
    for (entry = entries; *entry && project->excluded_count < PROJECT_MAX_EXCLUDED;
         entry += strlen(entry) + 1) {
        char *equals = strchr(entry, '=');
        if (!equals || !equals[1])
            continue;
        absolute_from(project->project_dir, equals + 1, project->excluded[project->excluded_count],
                      MAX_PATH);
        project->excluded_count++;
    }
}

static void relative_to_project(const StudioProject *project, const char *path, char *relative,
                                size_t capacity)
{
    size_t length = strlen(project->project_dir);

    if (path_is_same_or_child(project->project_dir, path) && path[length]) {
        const char *suffix = path + length;
        while (*suffix == '\\' || *suffix == '/')
            suffix++;
        snprintf(relative, capacity, ".\\%s", suffix);
    } else {
        snprintf(relative, capacity, "%s", path);
    }
}

static int save_excluded_paths(const StudioProject *project)
{
    char key[32];
    char path[MAX_PATH];

    if (!WritePrivateProfileStringA("Excluded", NULL, NULL, project->project_file))
        return 0;
    for (int index = 0; index < project->excluded_count; index++) {
        snprintf(key, sizeof(key), "Path%d", index + 1);
        relative_to_project(project, project->excluded[index], path, sizeof(path));
        if (!WritePrivateProfileStringA("Excluded", key, path, project->project_file))
            return 0;
    }
    return 1;
}

int project_load(StudioProject *project, const char *path)
{
    char value[2048];

    memset(project, 0, sizeof(*project));
    if (!GetFullPathNameA(path, MAX_PATH, project->project_file, NULL))
        return 0;
    if (GetFileAttributesA(project->project_file) == INVALID_FILE_ATTRIBUTES)
        return 0;
    directory_of(project->project_file, project->project_dir, sizeof(project->project_dir));

    GetPrivateProfileStringA("Project", "Name", "QuickJS Project", project->name,
                             sizeof(project->name), project->project_file);

    GetPrivateProfileStringA("Project", "Executable", "", value, sizeof(value),
                             project->project_file);
    absolute_from(project->project_dir, value, project->executable, sizeof(project->executable));

    GetPrivateProfileStringA("Project", "WorkingDirectory", ".", value, sizeof(value),
                             project->project_file);
    absolute_from(project->project_dir, value, project->working_directory,
                  sizeof(project->working_directory));

    GetPrivateProfileStringA("Project", "Entry", "", value, sizeof(value), project->project_file);
    absolute_from(project->project_dir, value, project->entry_script,
                  sizeof(project->entry_script));

    GetPrivateProfileStringA("Project", "ScriptRoot", ".", value, sizeof(value),
                             project->project_file);
    absolute_from(project->project_dir, value, project->script_root, sizeof(project->script_root));

    GetPrivateProfileStringA("Project", "HostApi", "", value, sizeof(value), project->project_file);
    absolute_from(project->project_dir, value, project->host_api, sizeof(project->host_api));
    load_excluded_paths(project);

    if (!read_raw_value(project->project_file, "Project", "Arguments", project->arguments,
                        sizeof(project->arguments)))
        GetPrivateProfileStringA("Project", "Arguments", "", project->arguments,
                                 sizeof(project->arguments), project->project_file);
    replace_macro(project->arguments, sizeof(project->arguments), "$(ProjectDir)",
                  project->project_dir);
    replace_macro(project->arguments, sizeof(project->arguments), "$(Entry)",
                  project->entry_script);
    return project->executable[0] && project->script_root[0];
}

int project_create(const char *path, const char *name, const char *executable)
{
    FILE *file = fopen(path, "wb");

    if (!file)
        return 0;
    fprintf(file,
            "[Project]\nName=%s\nExecutable=%s\nArguments=\"$(Entry)\"\n"
            "WorkingDirectory=.\nScriptRoot=.\nEntry=main.js\nHostApi=\n",
            name, executable);
    if (fclose(file) != 0)
        return 0;
    return 1;
}

int project_is_excluded(const StudioProject *project, const char *path)
{
    for (int index = 0; index < project->excluded_count; index++) {
        if (path_is_same_or_child(project->excluded[index], path))
            return 1;
    }
    return 0;
}

int project_exclude(StudioProject *project, const char *path)
{
    if (project_is_excluded(project, path))
        return 1;
    if (project->excluded_count >= PROJECT_MAX_EXCLUDED)
        return 0;
    snprintf(project->excluded[project->excluded_count], MAX_PATH, "%s", path);
    project->excluded_count++;
    if (!save_excluded_paths(project)) {
        project->excluded_count--;
        return 0;
    }
    return 1;
}

int project_include(StudioProject *project, const char *path)
{
    int output = 0;
    int changed = 0;

    for (int index = 0; index < project->excluded_count; index++) {
        if (path_is_same_or_child(project->excluded[index], path)) {
            changed = 1;
            continue;
        }
        if (output != index)
            snprintf(project->excluded[output], MAX_PATH, "%s", project->excluded[index]);
        output++;
    }
    if (!changed)
        return 1;
    project->excluded_count = output;
    return save_excluded_paths(project);
}

int project_save_settings(const StudioProject *project)
{
    return WritePrivateProfileStringA("Project", "Executable", project->executable,
                                      project->project_file) &&
           WritePrivateProfileStringA("Project", "Arguments", project->arguments,
                                      project->project_file) &&
           WritePrivateProfileStringA("Project", "WorkingDirectory", project->working_directory,
                                      project->project_file) &&
           WritePrivateProfileStringA("Project", "ScriptRoot", project->script_root,
                                      project->project_file) &&
           WritePrivateProfileStringA("Project", "Entry", project->entry_script,
                                      project->project_file) &&
           WritePrivateProfileStringA("Project", "HostApi", project->host_api,
                                      project->project_file);
}
