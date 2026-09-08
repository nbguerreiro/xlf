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

    /* Selection follows each directory when navigating away and back. */
    list.selected = 2;
    load_directory(&list, subdir);
    assert(list.count == 2);
    assert(list.selected == 0);

    list.selected = 1;
    load_directory(&list, dir);
    assert(list.selected == 2);
    assert(strcmp(list.entries[list.selected].name, "a.txt") == 0);

    load_directory(&list, subdir);
    assert(list.selected == 1);
    assert(strcmp(list.entries[list.selected].name, "b.txt") == 0);

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
