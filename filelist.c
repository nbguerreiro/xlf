#define _POSIX_C_SOURCE 200809L
#include "filelist.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void init_file_list(FileList *list, const char *path) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(path);
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
    list->entries = NULL;
    list->path = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
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

    for (int i = 0; i < list->count; i++) {
        free(list->entries[i].name);
    }
    free(list->entries);
    free(list->path);

    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(path);
    if (!list->path) list->path = NULL;

    if ((dir = opendir(path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 ||
                strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            char *fullpath = path_join(path, ent->d_name);
            if (!fullpath) continue;

            if (stat(fullpath, &st) == 0) {
                if (list->count >= list->capacity) {
                    size_t newcap = list->capacity == 0 ? 16 : list->capacity * 2;
                    FileEntry *tmp = realloc(list->entries,
                                             newcap * sizeof(FileEntry));
                    if (!tmp) {
                        break;
                    }
                    list->entries = tmp;
                    list->capacity = newcap;
                }

                char *name = strdup(ent->d_name);
                if (!name) continue;

                list->entries[list->count].name = name;
                list->entries[list->count].is_dir = S_ISDIR(st.st_mode);
                list->count++;
            }
            free(fullpath);
        }
        closedir(dir);
    }

    if (list->entries && list->count > 0) {
        qsort(list->entries, list->count, sizeof(FileEntry), compare_entries);
    }
}
