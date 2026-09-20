#define _POSIX_C_SOURCE 200809L
#include "filelist.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_file(const char *path) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("test\n", f);
    fclose(f);
}

int main(void) {
    char template[] = "/tmp/xlf-filelist-test-XXXXXX";
    char *dir = mkdtemp(template);
    assert(dir != NULL);

    char subdir[4096], sub_a[4096], sub_b[4096],
         file_a[4096], file_b[4096], hidden[4096];
    snprintf(subdir, sizeof(subdir), "%s/subdir", dir);
    snprintf(sub_a, sizeof(sub_a), "%s/subdir/a.txt", dir);
    snprintf(sub_b, sizeof(sub_b), "%s/subdir/b.txt", dir);
    snprintf(file_a, sizeof(file_a), "%s/a.txt", dir);
    snprintf(file_b, sizeof(file_b), "%s/b.txt", dir);
    snprintf(hidden, sizeof(hidden), "%s/.hidden", dir);

    assert(mkdir(subdir, 0700) == 0);
    make_file(sub_a);
    make_file(sub_b);
    make_file(file_a);
    make_file(file_b);
    make_file(hidden);

    FileList list;
    init_file_list(&list, dir);
    assert(list.entries == NULL);
    assert(list.count == 0);
    assert(list.selected == 0);
    assert(list.path != NULL);

    char *joined = path_join("/tmp/foo/", "bar");
    assert(joined != NULL);
    assert(strcmp(joined, "/tmp/foo/bar") == 0);
    free(joined);

    joined = path_join("/tmp/foo", "bar");
    assert(joined != NULL);
    assert(strcmp(joined, "/tmp/foo/bar") == 0);
    free(joined);

    char *absolute = get_absolute_path(dir);
    assert(absolute != NULL);
    assert(absolute[0] == '/');
    assert(strcmp(absolute, dir) == 0);
    free(absolute);

    absolute = get_absolute_path("/tmp/xlf-filelist-test-does-not-exist");
    assert(absolute == NULL);

    load_directory(&list, dir);

    /* Directories sort before regular files; hidden files are retained. */
    assert(list.count == 4);
    assert(strcmp(list.entries[0].name, "subdir") == 0);
    assert(list.entries[0].is_dir);
    assert(strcmp(list.entries[1].name, ".hidden") == 0);
    assert(!list.entries[1].is_dir);
    assert(strcmp(list.entries[2].name, "a.txt") == 0);
    assert(!list.entries[2].is_dir);
    assert(strcmp(list.entries[3].name, "b.txt") == 0);
    assert(!list.entries[3].is_dir);

    /* Multiselection can mark several entries and clear them as a group. */
    assert(count_marked_files(&list) == 0);
    toggle_file_mark(&list, 1);
    toggle_file_mark(&list, 3);
    assert(list.entries[1].marked);
    assert(list.entries[3].marked);
    assert(count_marked_files(&list) == 2);
    toggle_file_mark(&list, 1);
    assert(!list.entries[1].marked);
    assert(count_marked_files(&list) == 1);
    clear_file_marks(&list);
    assert(count_marked_files(&list) == 0);

    /* Selection history: entering a directory remembers where you were;
       the first visit to a directory starts on the first item. */
    list.selected = 2;
    load_directory(&list, subdir);
    assert(list.count == 2);
    assert(list.selected == 0);

    list.selected = 1;
    /* Going up always selects the child directory we just exited,
       overriding any remembered selection in the parent. */
    load_directory(&list, dir);
    assert(list.selected == 0);
    assert(strcmp(list.entries[list.selected].name, "subdir") == 0);

    /* Going down again restores the remembered selection. */
    load_directory(&list, subdir);
    assert(list.selected == 1);
    assert(strcmp(list.entries[list.selected].name, "b.txt") == 0);

    /* First-time up: parent was never visited (no remembered selection),
       so it must still land on the child directory that contains us. */
    char fresh_parent[4096], fresh_child[4096], fresh_file[4096];
    snprintf(fresh_parent, sizeof(fresh_parent), "%s/freshup", dir);
    assert(mkdir(fresh_parent, 0700) == 0);
    snprintf(fresh_child, sizeof(fresh_child), "%s/inner", fresh_parent);
    assert(mkdir(fresh_child, 0700) == 0);
    snprintf(fresh_file, sizeof(fresh_file), "%s/x.txt", fresh_child);
    make_file(fresh_file);

    FileList fresh;
    init_file_list(&fresh, fresh_child);
    load_directory(&fresh, fresh_child);
    assert(fresh.count == 1);
    assert(fresh.selected == 0);

    load_directory(&fresh, fresh_parent);
    assert(fresh.count == 1);
    assert(fresh.entries[fresh.selected].is_dir);
    assert(strcmp(fresh.entries[fresh.selected].name, "inner") == 0);
    free_file_list(&fresh);

    unlink(fresh_file);
    rmdir(fresh_child);
    rmdir(fresh_parent);

    free_file_list(&list);
    assert(list.entries == NULL);
    assert(list.path == NULL);
    assert(list.count == 0);

    unlink(sub_b);
    unlink(sub_a);
    unlink(hidden);
    unlink(file_b);
    unlink(file_a);
    rmdir(subdir);
    rmdir(dir);

    puts("filelist tests: ok");
    return 0;
}
