#ifndef FILELIST_H
#define FILELIST_H

#include <stddef.h>

typedef struct {
    char *name;
    int is_dir;
    int marked;
} FileEntry;

typedef struct {
    char *path;
    char *name;
} SelectionMemory;

typedef struct {
    FileEntry *entries;
    int count;
    int capacity;
    int selected;
    char *path;
    SelectionMemory *selection_history;
    size_t selection_history_count;
    size_t selection_history_capacity;
} FileList;

void init_file_list(FileList *list, const char *path);
void free_file_list(FileList *list);
int compare_entries(const void *a, const void *b);
void load_directory(FileList *list, const char *path);
void toggle_file_mark(FileList *list, int index);
void clear_file_marks(FileList *list);
int count_marked_files(const FileList *list);

#endif
