#define _POSIX_C_SOURCE 200809L
#include "filelist.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

void init_file_list(FileList *list, const char *path) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(path);
    list->selection_history = NULL;
    list->selection_history_count = 0;
    list->selection_history_capacity = 0;
}

static void remember_selection(FileList *list) {
    if (!list || !list->path || !list->entries ||
        list->selected < 0 || list->selected >= list->count) {
        return;
    }

    const char *name = list->entries[list->selected].name;
    for (size_t i = 0; i < list->selection_history_count; ++i) {
        if (strcmp(list->selection_history[i].path, list->path) == 0) {
            char *copy = strdup(name);
            if (!copy) return;
            free(list->selection_history[i].name);
            list->selection_history[i].name = copy;
            return;
        }
    }

    if (list->selection_history_count >= list->selection_history_capacity) {
        size_t new_capacity = list->selection_history_capacity == 0
            ? 8 : list->selection_history_capacity * 2;
        SelectionMemory *tmp = realloc(
            list->selection_history,
            new_capacity * sizeof(*list->selection_history));
        if (!tmp) return;
        list->selection_history = tmp;
        list->selection_history_capacity = new_capacity;
    }

    char *path_copy = strdup(list->path);
    char *name_copy = strdup(name);
    if (!path_copy || !name_copy) {
        free(path_copy);
        free(name_copy);
        return;
    }

    list->selection_history[list->selection_history_count].path = path_copy;
    list->selection_history[list->selection_history_count].name = name_copy;
    list->selection_history_count++;
}

static void restore_selection(FileList *list) {
    list->selected = 0;
    if (!list->path || !list->entries || list->count <= 0) return;

    for (size_t i = 0; i < list->selection_history_count; ++i) {
        if (strcmp(list->selection_history[i].path, list->path) != 0) {
            continue;
        }

        for (int j = 0; j < list->count; ++j) {
            if (strcmp(list->entries[j].name,
                       list->selection_history[i].name) == 0) {
                list->selected = j;
                return;
            }
        }
        return;
    }
}

/* When loading a parent directory, select the child directory we just left.
   This is deliberately separate from the general selection history: the
   parent may already have a remembered selection, but going up should always
   land on the directory that contains us. */
static void select_previous_directory(FileList *list, const char *old_path) {
    if (!list || !list->path || !old_path || !*old_path) return;

    const char *slash = strrchr(old_path, '/');
    if (!slash || slash[1] == '\0') return;

    size_t parent_len = (slash == old_path) ? 1U : (size_t)(slash - old_path);
    if (strlen(list->path) != parent_len ||
        strncmp(list->path, old_path, parent_len) != 0) {
        return;
    }

    const char *child_name = slash + 1;
    for (int i = 0; i < list->count; ++i) {
        if (strcmp(list->entries[i].name, child_name) == 0 &&
            list->entries[i].is_dir) {
            list->selected = i;
            return;
        }
    }
}

void free_file_list(FileList *list) {
    if (!list) return;
    if (list->entries) {
        for (int i = 0; i < list->count; i++) {
            free(list->entries[i].name);
        }
    }
    free(list->entries);
    free(list->path);
    for (size_t i = 0; i < list->selection_history_count; ++i) {
        free(list->selection_history[i].path);
        free(list->selection_history[i].name);
    }
    free(list->selection_history);
    list->selection_history = NULL;
    list->selection_history_count = 0;
    list->selection_history_capacity = 0;
    list->entries = NULL;
    list->path = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
}

void toggle_file_mark(FileList *list, int index) {
    if (!list || index < 0 || index >= list->count) return;
    if (strcmp(list->entries[index].name, "..") == 0) return;
    list->entries[index].marked = !list->entries[index].marked;
}

void clear_file_marks(FileList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; ++i) {
        list->entries[i].marked = 0;
    }
}

int count_marked_files(const FileList *list) {
    if (!list) return 0;
    int count = 0;
    for (int i = 0; i < list->count; ++i) {
        if (list->entries[i].marked) ++count;
    }
    return count;
}

int compare_entries(const void *a, const void *b) {
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;

    if (ea->is_dir != eb->is_dir) {
        return eb->is_dir - ea->is_dir;
    }
    return strcmp(ea->name, eb->name);
}

void load_directory(FileList *list, const char *path) {
    DIR *dir;
    const struct dirent *ent;
    struct stat st;
    char cwd[PATH_MAX];
    const char *load_path = path;
    char *old_path = list->path ? strdup(list->path) : NULL;

    /* Keep the initial directory absolute so parent navigation never
       transitions through the relative paths "." and "..". */
    if (path && strcmp(path, ".") == 0 && getcwd(cwd, sizeof(cwd)) != NULL) {
        load_path = cwd;
    }

    remember_selection(list);

    for (int i = 0; i < list->count; i++) {
        free(list->entries[i].name);
    }
    free(list->entries);
    free(list->path);

    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(load_path);
    if (!list->path) list->path = NULL;

    if ((dir = opendir(load_path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            char *fullpath = path_join(load_path, ent->d_name);
            if (!fullpath) continue;

            if (stat(fullpath, &st) == 0) {
                if (list->count >= list->capacity) {
                    size_t newcap = list->capacity == 0 ? 16 : list->capacity * 2;
                    FileEntry *tmp = realloc(list->entries,
                                             newcap * sizeof(FileEntry));
                    if (!tmp) {
                        free(fullpath);
                        break;
                    }
                    list->entries = tmp;
                    list->capacity = newcap;
                }

                char *name = strdup(ent->d_name);
                if (!name) {
                    free(fullpath);
                    continue;
                }

                list->entries[list->count].name = name;
                list->entries[list->count].is_dir = S_ISDIR(st.st_mode);
                list->entries[list->count].marked = 0;
                list->count++;
            }
            free(fullpath);
        }
        closedir(dir);
    }

    if (list->entries && list->count > 0) {
        qsort(list->entries, list->count, sizeof(FileEntry), compare_entries);
    }

    restore_selection(list);
    select_previous_directory(list, old_path);
    free(old_path);
}