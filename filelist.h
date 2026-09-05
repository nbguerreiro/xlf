#ifndef FILELIST_H
#define FILELIST_H

#include <stddef.h>

typedef struct {
    char *name;
    int is_dir;
} FileEntry;

typedef struct {
    FileEntry *entries;
    int count;
    int capacity;
    int selected;
    char *path;
} FileList;

void init_file_list(FileList *list, const char *path);
void free_file_list(FileList *list);
int compare_entries(const void *a, const void *b);
void load_directory(FileList *list, const char *path);

#endif
