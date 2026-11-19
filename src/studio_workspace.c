#include "studio_internal.h"

#include "Scintilla.h"
#include "editor.h"
#include "input_dialog.h"
#include "project_settings.h"

#include <commdlg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void directory_of_path(const char *path, char *directory, size_t capacity);

void studio_show_left_page(int page)
{
    studio.left_page = page;
    TabCtrl_SetCurSel(studio.left_tabs, page);
    if (!studio.project_loaded) {
        TreeView_DeleteAllItems(studio.tree);
        return;
    }
    if (page == PAGE_CLASS_VIEW)
        studio_populate_class_tree();
    else
        studio_populate_project_tree();
}

int studio_load_project(const char *path)
{
    StudioProject project;
    if (editor_is_modified(studio.editor)) {
        int answer = MessageBoxA(studio.window, "Save the current script?", "QuickJS Studio",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !editor_save(studio.editor)))
            return 0;
    }
    if (!project_load(&project, path)) {
        studio_show_error(studio.window, "Cannot load the project file.");
        return 0;
    }
    if (debug_session_is_running(&studio.debug))
        debug_session_stop(&studio.debug);
    studio_reset_document_navigation();
    editor_reset(studio.editor);
    ShowWindow(studio.editor, SW_HIDE);
    ShowWindow(studio.document_background, SW_SHOW);
    studio_set_document_close_button(0);
    studio.project = project;
    studio.paused = 0;
    studio.breakpoint_count = 0;
    studio.project_loaded = 1;
    studio_refresh_navigation_files();
    studio_refresh_navigation_symbols();
    studio_show_left_page(PAGE_FILE_VIEW);
    studio_output_append("Project loaded: %s\r\n", studio.project.project_file);
    if (studio.project.entry_script[0])
        studio_open_script(studio.project.entry_script);
    else
        studio_layout_controls(studio.window);
    return 1;
}

void studio_close_workspace(void)
{
    if (!studio.project_loaded)
        return;
    if (editor_is_modified(studio.editor)) {
        int answer = MessageBoxA(studio.window, "Save the current script?", "QuickJS Studio",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !editor_save(studio.editor)))
            return;
    }
    if (debug_session_is_running(&studio.debug))
        debug_session_stop(&studio.debug);
    studio_reset_document_navigation();
    editor_reset(studio.editor);
    ShowWindow(studio.editor, SW_HIDE);
    ShowWindow(studio.document_background, SW_SHOW);
    studio_set_document_close_button(0);
    studio_layout_controls(studio.window);
    TreeView_DeleteAllItems(studio.tree);
    memset(&studio.project, 0, sizeof(studio.project));
    studio.project_loaded = 0;
    studio.navigation_file_count = 0;
    studio.wizard_symbol_count = 0;
    studio.breakpoint_count = 0;
    studio_refresh_navigation_symbols();
    studio_update_editor_title(NULL);
    studio_output_append("Workspace closed.\r\n");
}

void studio_close_current_script(void)
{
    if (!editor_path()[0])
        return;
    if (editor_is_modified(studio.editor)) {
        int answer = MessageBoxA(studio.window, "Save the current script?", "QuickJS Studio",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL || (answer == IDYES && !editor_save(studio.editor)))
            return;
    }
    if (debug_session_is_running(&studio.debug))
        debug_session_stop(&studio.debug);
    studio_remember_current_document();
    studio_sync_current_breakpoints();
    editor_reset(studio.editor);
    ShowWindow(studio.editor, SW_HIDE);
    ShowWindow(studio.document_background, SW_SHOW);
    studio_set_document_close_button(0);
    studio_layout_controls(studio.window);
    studio_update_editor_title(NULL);
    studio_refresh_navigation_symbols();
    studio_output_append("Document closed.\r\n");
}

void studio_new_project(void)
{
    char path[MAX_PATH] = "QuickJS Project.qsp";
    char directory[MAX_PATH];
    char name[128];
    char executable[MAX_PATH];
    char main_script[MAX_PATH];
    char *extension;
    char *slash;
    OPENFILENAMEA dialog = {0};
    FILE *file;

    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "QuickJS Studio Project (*.qsp)\0*.qsp\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrDefExt = "qsp";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&dialog))
        return;
    directory_of_path(path, directory, sizeof(directory));
    slash = strrchr(path, '\\');
    snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
    extension = strrchr(name, '.');
    if (extension)
        *extension = '\0';
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    slash = strrchr(executable, '\\');
    if (slash)
        snprintf(slash + 1, (size_t)(executable + sizeof(executable) - slash - 1), "qjs-host.exe");
    if (!project_create(path, name, executable)) {
        studio_show_error(studio.window, "Cannot create the project file.");
        return;
    }
    if (snprintf(main_script, sizeof(main_script), "%s\\main.js", directory) >=
        (int)sizeof(main_script)) {
        studio_show_error(studio.window, "The project path is too long.");
        return;
    }
    file = fopen(main_script, "rb");
    if (file) {
        fclose(file);
    } else {
        file = fopen(main_script, "wb");
        if (!file) {
            studio_show_error(studio.window,
                              "The project was created, but main.js could not be created.");
            return;
        }
        fputs("'use strict';\n\nconsole.log('QuickJS Studio project');\n", file);
        fclose(file);
    }
    if (studio_load_project(path))
        studio_output_append("Created project: %s\r\n", path);
}

TreeTarget *studio_selected_tree_target(void)
{
    TVITEMA item = {0};

    item.hItem = TreeView_GetSelection(studio.tree);
    item.mask = TVIF_PARAM;
    if (!item.hItem || !TreeView_GetItem(studio.tree, &item) || !item.lParam)
        return NULL;
    return (TreeTarget *)item.lParam;
}

static void directory_of_path(const char *path, char *directory, size_t capacity)
{
    char *slash;

    snprintf(directory, capacity, "%s", path ? path : "");
    slash = strrchr(directory, '\\');
    if (!slash)
        slash = strrchr(directory, '/');
    if (slash)
        *slash = '\0';
}

static void selected_project_directory(char *directory, size_t capacity)
{
    TreeTarget *target = studio.left_page == PAGE_FILE_VIEW ? studio_selected_tree_target() : NULL;

    if (!target) {
        snprintf(directory, capacity, "%s", studio.project.script_root);
    } else if (target->is_directory) {
        snprintf(directory, capacity, "%s", target->path);
    } else {
        directory_of_path(target->path, directory, capacity);
    }
}

static int path_is_same_or_child(const char *parent, const char *path)
{
    size_t length = strlen(parent);

    while (length > 0 && (parent[length - 1] == '\\' || parent[length - 1] == '/'))
        length--;
    return _strnicmp(parent, path, length) == 0 &&
           (path[length] == '\0' || path[length] == '\\' || path[length] == '/');
}

static void replace_path_prefix(char *path, size_t capacity, const char *old_prefix,
                                const char *new_prefix)
{
    char updated[MAX_PATH];
    size_t old_length = strlen(old_prefix);

    if (!path_is_same_or_child(old_prefix, path))
        return;
    snprintf(updated, sizeof(updated), "%s%s", new_prefix, path + old_length);
    snprintf(path, capacity, "%s", updated);
}

void studio_remove_selected_project_item(void)
{
    TreeTarget *target = studio_selected_tree_target();
    char message[MAX_PATH + 160];

    if (!target || !target->is_physical)
        return;
    snprintf(message, sizeof(message),
             "Remove '%s' from this project?\r\n\r\nThe file or folder will remain on disk.",
             target->path);
    if (MessageBoxA(studio.window, message, "Remove From Project", MB_YESNO | MB_ICONQUESTION) !=
        IDYES)
        return;
    if (!project_exclude(&studio.project, target->path)) {
        studio_show_error(studio.window, "Cannot update the project file.");
        return;
    }
    studio_output_append("Removed from project (kept on disk): %s\r\n", target->path);
    studio_refresh_project_views();
}

void studio_rename_selected_project_item(void)
{
    TreeTarget *target = studio_selected_tree_target();
    char old_path[MAX_PATH];
    char new_path[MAX_PATH];
    char directory[MAX_PATH];
    char name[MAX_PATH];
    char current_path[MAX_PATH];
    const char *base;

    if (!target || !target->is_physical)
        return;
    snprintf(old_path, sizeof(old_path), "%s", target->path);
    base = strrchr(old_path, '\\');
    if (!base)
        base = strrchr(old_path, '/');
    snprintf(name, sizeof(name), "%s", base ? base + 1 : old_path);
    if (!input_dialog_show(studio.window, "Rename", "New name:", name, name, sizeof(name)))
        return;
    if (!name[0] || strpbrk(name, "\\/:*?\"<>|") || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        studio_show_error(studio.window, "The name contains invalid characters.");
        return;
    }
    directory_of_path(old_path, directory, sizeof(directory));
    if (snprintf(new_path, sizeof(new_path), "%s\\%s", directory, name) >= (int)sizeof(new_path)) {
        studio_show_error(studio.window, "The path is too long.");
        return;
    }
    if (_stricmp(old_path, new_path) == 0)
        return;
    if (editor_path()[0] && path_is_same_or_child(old_path, editor_path()) &&
        editor_is_modified(studio.editor) && !editor_save(studio.editor)) {
        studio_show_error(studio.window, "Cannot save the current script before renaming.");
        return;
    }
    if (!MoveFileA(old_path, new_path)) {
        studio_show_error(studio.window, GetLastError() == ERROR_ALREADY_EXISTS
                                             ? "A file or folder with that name already exists."
                                             : "Cannot rename the project item.");
        return;
    }
    snprintf(current_path, sizeof(current_path), "%s", editor_path());
    if (current_path[0] && path_is_same_or_child(old_path, current_path)) {
        replace_path_prefix(current_path, sizeof(current_path), old_path, new_path);
        if (!editor_save_as(studio.editor, current_path)) {
            studio_show_error(studio.window,
                              "The item was renamed, but the editor path could not be updated.");
        } else {
            studio_update_editor_title(current_path);
        }
    }
    if (path_is_same_or_child(old_path, studio.project.entry_script)) {
        replace_path_prefix(studio.project.entry_script, sizeof(studio.project.entry_script),
                            old_path, new_path);
        WritePrivateProfileStringA("Project", "Entry", studio.project.entry_script,
                                   studio.project.project_file);
    }
    for (int index = 0; index < studio.breakpoint_count; index++)
        replace_path_prefix(studio.breakpoints[index].path, sizeof(studio.breakpoints[index].path),
                            old_path, new_path);
    studio_output_append("Renamed: %s -> %s\r\n", old_path, new_path);
    studio_refresh_project_views();
}

static int path_is_in_script_root(const char *path)
{
    char root[MAX_PATH];
    char candidate[MAX_PATH];
    size_t length;

    if (!GetFullPathNameA(studio.project.script_root, ARRAYSIZE(root), root, NULL) ||
        !GetFullPathNameA(path, ARRAYSIZE(candidate), candidate, NULL))
        return 0;
    length = strlen(root);
    while (length > 0 && (root[length - 1] == '\\' || root[length - 1] == '/'))
        root[--length] = '\0';
    return _strnicmp(root, candidate, length) == 0 &&
           (candidate[length] == '\0' || candidate[length] == '\\' || candidate[length] == '/');
}

void studio_refresh_project_views(void)
{
    studio_refresh_navigation_files();
    studio_refresh_navigation_symbols();
    studio_show_left_page(studio.left_page);
}

void studio_new_script_file(void)
{
    char directory[MAX_PATH];
    char path[MAX_PATH] = "script.js";
    OPENFILENAMEA dialog = {0};
    FILE *file;

    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    selected_project_directory(directory, sizeof(directory));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "JavaScript Source (*.js)\0*.js\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrInitialDir = directory;
    dialog.lpstrDefExt = "js";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&dialog))
        return;
    if (!path_is_in_script_root(path)) {
        studio_show_error(studio.window, "The new script must be inside the project's ScriptRoot.");
        return;
    }
    file = fopen(path, "wb");
    if (!file) {
        studio_show_error(studio.window, "Cannot create the JavaScript file.");
        return;
    }
    fclose(file);
    studio_refresh_project_views();
    studio_open_script(path);
    studio_output_append("Created file: %s\r\n", path);
}

void studio_new_project_folder(void)
{
    char directory[MAX_PATH];
    char name[MAX_PATH];
    char path[MAX_PATH];

    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    selected_project_directory(directory, sizeof(directory));
    if (!input_dialog_show(studio.window, "New Folder", "Folder name:", "New Folder", name,
                           sizeof(name)))
        return;
    if (!name[0] || strpbrk(name, "\\/:*?\"<>|") || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        studio_show_error(studio.window, "The folder name contains invalid characters.");
        return;
    }
    if (snprintf(path, sizeof(path), "%s\\%s", directory, name) >= (int)sizeof(path)) {
        studio_show_error(studio.window, "The folder path is too long.");
        return;
    }
    if (!CreateDirectoryA(path, NULL)) {
        studio_show_error(studio.window, GetLastError() == ERROR_ALREADY_EXISTS
                                             ? "A file or folder with that name already exists."
                                             : "Cannot create the project folder.");
        return;
    }
    studio_refresh_project_views();
    studio_output_append("Created folder: %s\r\n", path);
}

static int copy_file_to_project(const char *source, const char *directory, char *target,
                                size_t capacity)
{
    const char *name = strrchr(source, '\\');
    DWORD attributes;

    if (!name)
        name = strrchr(source, '/');
    name = name ? name + 1 : source;
    if (snprintf(target, capacity, "%s\\%s", directory, name) >= (int)capacity)
        return 0;
    if (_stricmp(source, target) == 0)
        return project_include(&studio.project, target);
    attributes = GetFileAttributesA(target);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        char message[MAX_PATH + 80];
        snprintf(message, sizeof(message), "%s already exists. Replace it?", target);
        if (MessageBoxA(studio.window, message, "Add Files to Folder",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
    }
    if (!CopyFileA(source, target, FALSE))
        return 0;
    return project_include(&studio.project, target);
}

void studio_add_files_to_project(void)
{
    char files[32768] = "";
    char directory[MAX_PATH];
    char last_target[MAX_PATH] = "";
    OPENFILENAMEA dialog = {0};
    char *name;
    int added = 0;

    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    selected_project_directory(directory, sizeof(directory));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "JavaScript Source (*.js)\0*.js\0All Files\0*.*\0";
    dialog.lpstrFile = files;
    dialog.nMaxFile = sizeof(files);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameA(&dialog))
        return;
    name = files + strlen(files) + 1;
    if (!*name) {
        added = copy_file_to_project(files, directory, last_target, sizeof(last_target));
    } else {
        char source[MAX_PATH];
        while (*name) {
            if (snprintf(source, sizeof(source), "%s\\%s", files, name) < (int)sizeof(source) &&
                copy_file_to_project(source, directory, last_target, sizeof(last_target)))
                added++;
            name += strlen(name) + 1;
        }
    }
    if (!added) {
        studio_set_status("No files were added");
        return;
    }
    studio_refresh_project_views();
    studio_output_append("Added %d file(s) to %s\r\n", added, directory);
    if (added == 1 && last_target[0])
        studio_open_script(last_target);
}

void studio_save_script_as(void)
{
    char path[MAX_PATH];
    char directory[MAX_PATH];
    OPENFILENAMEA dialog = {0};

    if (!editor_path()[0]) {
        studio_new_script_file();
        return;
    }
    snprintf(path, sizeof(path), "%s",
             strrchr(editor_path(), '\\') ? strrchr(editor_path(), '\\') + 1 : editor_path());
    directory_of_path(editor_path(), directory, sizeof(directory));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "JavaScript Source (*.js)\0*.js\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrInitialDir = directory;
    dialog.lpstrDefExt = "js";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameA(&dialog))
        return;
    if (studio.project_loaded && !path_is_in_script_root(path)) {
        studio_show_error(studio.window, "Save the script inside the project's ScriptRoot.");
        return;
    }
    if (!editor_save_as(studio.editor, path)) {
        studio_show_error(studio.window, "Cannot save the JavaScript file.");
        return;
    }
    studio_update_editor_title(path);
    studio_refresh_project_views();
    studio_refresh_navigation_symbols();
}

void studio_show_project_properties(void)
{
    StudioProject updated;

    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    if (!project_settings_show(studio.window, &studio.project))
        return;
    if (!project_save_settings(&studio.project) ||
        !project_load(&updated, studio.project.project_file)) {
        studio_show_error(studio.window, "Cannot save the project settings.");
        return;
    }
    studio.project = updated;
    studio_refresh_project_views();
    studio_output_append("Project settings saved.\r\n");
}

int studio_find_next_text(void)
{
    LRESULT start;
    LRESULT length;
    LRESULT found;
    LRESULT end;

    if (!studio.find_text[0])
        return 0;
    start = SendMessageA(studio.editor, SCI_GETCURRENTPOS, 0, 0);
    length = SendMessageA(studio.editor, SCI_GETLENGTH, 0, 0);
    SendMessageA(studio.editor, SCI_SETTARGETSTART, (WPARAM)start, 0);
    SendMessageA(studio.editor, SCI_SETTARGETEND, (WPARAM)length, 0);
    SendMessageA(studio.editor, SCI_SETSEARCHFLAGS, 0, 0);
    found = SendMessageA(studio.editor, SCI_SEARCHINTARGET, strlen(studio.find_text),
                         (LPARAM)studio.find_text);
    if (found < 0 && start > 0) {
        SendMessageA(studio.editor, SCI_SETTARGETSTART, 0, 0);
        SendMessageA(studio.editor, SCI_SETTARGETEND, (WPARAM)start, 0);
        found = SendMessageA(studio.editor, SCI_SEARCHINTARGET, strlen(studio.find_text),
                             (LPARAM)studio.find_text);
    }
    if (found < 0) {
        studio_set_status("Text not found");
        return 0;
    }
    end = SendMessageA(studio.editor, SCI_GETTARGETEND, 0, 0);
    SendMessageA(studio.editor, SCI_SETSEL, (WPARAM)found, (LPARAM)end);
    SendMessageA(studio.editor, SCI_SCROLLCARET, 0, 0);
    return 1;
}

void studio_find_text_dialog(void)
{
    char selected[256];
    char initial[256];

    if (editor_selected_text(studio.editor, selected, sizeof(selected)))
        snprintf(initial, sizeof(initial), "%s", selected);
    else
        snprintf(initial, sizeof(initial), "%s", studio.find_text);
    if (!input_dialog_show(studio.window, "Find", "Find what:", initial, studio.find_text,
                           sizeof(studio.find_text)))
        return;
    if (!studio.find_text[0])
        return;
    studio_find_next_text();
}

void studio_replace_text_dialog(void)
{
    char search[256];
    char replacement[256];
    LRESULT position;
    int replacements = 0;

    snprintf(search, sizeof(search), "%s", studio.find_text);
    if (!input_dialog_show(studio.window, "Replace", "Find what:", search, search, sizeof(search)))
        return;
    if (!search[0])
        return;
    if (!input_dialog_show(studio.window, "Replace", "Replace with:", "", replacement,
                           sizeof(replacement)))
        return;
    snprintf(studio.find_text, sizeof(studio.find_text), "%s", search);
    SendMessageA(studio.editor, SCI_SETTARGETSTART, 0, 0);
    SendMessageA(studio.editor, SCI_SETTARGETEND,
                 (WPARAM)SendMessageA(studio.editor, SCI_GETLENGTH, 0, 0), 0);
    SendMessageA(studio.editor, SCI_SETSEARCHFLAGS, 0, 0);
    while ((position = SendMessageA(studio.editor, SCI_SEARCHINTARGET, strlen(search),
                                    (LPARAM)search)) >= 0) {
        LRESULT next;
        SendMessageA(studio.editor, SCI_REPLACETARGET, strlen(replacement), (LPARAM)replacement);
        next = position + (LRESULT)strlen(replacement);
        SendMessageA(studio.editor, SCI_SETTARGETSTART, (WPARAM)next, 0);
        SendMessageA(studio.editor, SCI_SETTARGETEND,
                     (WPARAM)SendMessageA(studio.editor, SCI_GETLENGTH, 0, 0), 0);
        replacements++;
    }
    {
        char status[80];
        snprintf(status, sizeof(status), "Replaced %d occurrence(s)", replacements);
        studio_set_status(status);
    }
}

void studio_find_in_files_dialog(void)
{
    char search[256];
    char selected[256];
    int matches = 0;

    if (!studio.project_loaded) {
        studio_show_error(studio.window, "Open a .qsp project first.");
        return;
    }
    if (editor_selected_text(studio.editor, selected, sizeof(selected)))
        snprintf(search, sizeof(search), "%s", selected);
    else
        snprintf(search, sizeof(search), "%s", studio.find_text);
    if (!input_dialog_show(studio.window, "Find in Files", "Find what:", search, search,
                           sizeof(search)) ||
        !search[0])
        return;
    snprintf(studio.find_text, sizeof(studio.find_text), "%s", search);
    studio_open_bottom_page(PAGE_OUTPUT);
    studio_output_append("Find in Files: %s\r\n", search);
    for (int index = 0; index < studio.navigation_file_count; index++) {
        FILE *file = fopen(studio.navigation_files[index].path, "rb");
        char line[8192];
        int line_number = 0;

        if (!file)
            continue;
        while (fgets(line, sizeof(line), file)) {
            char *end;
            line_number++;
            if (!strstr(line, search))
                continue;
            end = line + strlen(line);
            while (end > line && (end[-1] == '\r' || end[-1] == '\n'))
                *--end = '\0';
            studio_output_append("%s(%d): %s\r\n", studio.navigation_files[index].path, line_number,
                                 line);
            matches++;
        }
        fclose(file);
    }
    studio_output_append("Find in Files complete: %d match(es).\r\n", matches);
}

void studio_go_to_line_dialog(void)
{
    char value[32];
    int line;
    int line_count;

    snprintf(value, sizeof(value), "%d", editor_current_line(studio.editor) + 1);
    if (!input_dialog_show(studio.window, "Go To", "Line number:", value, value, sizeof(value)))
        return;
    line = atoi(value);
    line_count = (int)SendMessageA(studio.editor, SCI_GETLINECOUNT, 0, 0);
    if (line < 1 || line > line_count) {
        studio_show_error(studio.window, "The line number is outside the current document.");
        return;
    }
    editor_goto_line(studio.editor, line - 1);
}

void studio_insert_file_as_text(void)
{
    char path[MAX_PATH] = "";
    OPENFILENAMEA dialog = {0};
    FILE *file;
    char *text;
    long length;

    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "Text Files\0*.js;*.json;*.txt;*.md\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&dialog))
        return;
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file)
            fclose(file);
        studio_show_error(studio.window, "Cannot read the selected file.");
        return;
    }
    text = malloc((size_t)length + 1);
    if (!text || fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        studio_show_error(studio.window, "Cannot read the selected file.");
        return;
    }
    fclose(file);
    text[length] = '\0';
    SendMessageA(studio.editor, SCI_REPLACESEL, 0, (LPARAM)text);
    free(text);
}

void studio_open_project_dialog(void)
{
    char path[MAX_PATH] = "";
    OPENFILENAMEA dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "QuickJS Studio Project (*.qsp)\0*.qsp\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&dialog))
        studio_load_project(path);
}

void studio_open_script_dialog(void)
{
    char path[MAX_PATH] = "";
    OPENFILENAMEA dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = studio.window;
    dialog.lpstrFilter = "JavaScript (*.js)\0*.js\0All Files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&dialog))
        studio_open_script(path);
}
