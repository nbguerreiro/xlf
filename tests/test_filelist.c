#define _POSIX_C_SOURCE 200809L
#include "filelist.h"

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

    char subdir[4096], file_a[4096], file_b[4096], hidden[4096];
    snprintf(subdir, sizeof(subdir), "%s/subdir", dir);
    snprintf(file_a, sizeof(file_a), "%s/a.txt", dir);
    snprintf(file_b, sizeof(file_b), "%s/b.txt", dir);
    snprintf(hidden, sizeof(hidden), "%s/.hidden", dir);

    assert(mkdir(subdir, 0700) == 0);
    make_file(file_a);
    make_file(file_b);
    make_file(hidden);

    FileList list;
    init_file_list(&list, dir);
    assert(list.entries == NULL);
    assert(list.count == 0);
    assert(list.selected == 0);
    assert(list.path != NULL);

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

    free_file_list(&list);
    assert(list.entries == NULL);
    assert(list.path == NULL);
    assert(list.count == 0);

    unlink(hidden);
    unlink(file_b);
    unlink(file_a);
    rmdir(subdir);
    rmdir(dir);

    puts("filelist tests: ok");
    return 0;
}
