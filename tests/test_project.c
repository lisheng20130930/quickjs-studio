#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "project.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char temporary[MAX_PATH];
    char directory[MAX_PATH];
    char project_path[MAX_PATH];
    char source_directory[MAX_PATH];
    char source_file[MAX_PATH];
    char host_api[MAX_PATH];
    StudioProject project;
    StudioProject reloaded;
    FILE *file;

    assert(GetTempPathA(sizeof(temporary), temporary) > 0);
    assert(GetTempFileNameA(temporary, "qsp", 0, directory) != 0);
    assert(DeleteFileA(directory));
    assert(CreateDirectoryA(directory, NULL));
    snprintf(project_path, sizeof(project_path), "%s\\sample.qsp", directory);
    snprintf(source_directory, sizeof(source_directory), "%s\\legacy", directory);
    snprintf(source_file, sizeof(source_file), "%s\\unused.js", source_directory);
    snprintf(host_api, sizeof(host_api), "%s\\engine.api.js", directory);
    assert(CreateDirectoryA(source_directory, NULL));
    file = fopen(source_file, "wb");
    assert(file != NULL);
    fputs("const unused = true;\n", file);
    fclose(file);
    file = fopen(host_api, "wb");
    assert(file != NULL);
    fputs("class Engine { static log(text) {} }\n", file);
    fclose(file);

    assert(project_create(project_path, "Project Test", "C:\\fake\\qjs-host.exe"));
    assert(project_load(&project, project_path));
    assert(strcmp(project.name, "Project Test") == 0);
    assert(project_exclude(&project, source_directory));
    assert(project_load(&reloaded, project_path));
    assert(project_is_excluded(&reloaded, source_file));
    assert(project_include(&reloaded, source_file));
    assert(project_load(&project, project_path));
    assert(!project_is_excluded(&project, source_file));

    snprintf(project.arguments, sizeof(project.arguments), "--mode=test \"$(Entry)\"");
    snprintf(project.host_api, sizeof(project.host_api), "%s", host_api);
    assert(project_save_settings(&project));
    assert(project_load(&reloaded, project_path));
    assert(strstr(reloaded.arguments, "--mode=test") != NULL);
    assert(_stricmp(reloaded.host_api, host_api) == 0);

    assert(DeleteFileA(source_file));
    assert(DeleteFileA(host_api));
    assert(RemoveDirectoryA(source_directory));
    assert(DeleteFileA(project_path));
    assert(RemoveDirectoryA(directory));
    puts("QuickJS Studio project create/settings/exclude/include: PASS");
    return 0;
}
