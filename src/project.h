#ifndef QJS_STUDIO_PROJECT_H
#define QJS_STUDIO_PROJECT_H

#include <windows.h>

#define PROJECT_MAX_EXCLUDED 256

typedef struct StudioProject {
    char project_file[MAX_PATH];
    char project_dir[MAX_PATH];
    char name[128];
    char executable[MAX_PATH];
    char arguments[2048];
    char working_directory[MAX_PATH];
    char script_root[MAX_PATH];
    char entry_script[MAX_PATH];
    char host_api[MAX_PATH];
    char excluded[PROJECT_MAX_EXCLUDED][MAX_PATH];
    int excluded_count;
} StudioProject;

int project_load(StudioProject *project, const char *path);
int project_create(const char *path, const char *name, const char *executable);
int project_is_excluded(const StudioProject *project, const char *path);
int project_exclude(StudioProject *project, const char *path);
int project_include(StudioProject *project, const char *path);
int project_save_settings(const StudioProject *project);

#endif
