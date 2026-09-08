#define _POSIX_C_SOURCE 200809L
#include "history.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HISTORY_LIMIT 100

static int ensure_directory(const char *path) {
    if (mkdir(path, 0700) == 0) return 0;
    return errno == EEXIST ? 0 : -1;
}

static int history_path(char *path, size_t size) {
    const char *state = getenv("XDG_STATE_HOME");
    if (!state || !*state) {
        const char *home = getenv("HOME");
        if (!home || !*home) return -1;
        int n = snprintf(path, size, "%s/.local/state/fm/history", home);
        if (n < 0 || (size_t)n >= size) return -1;

        char dir[PATH_MAX];
        n = snprintf(dir, sizeof(dir), "%s/.local", home);
        if (n < 0 || (size_t)n >= sizeof(dir) || ensure_directory(dir) != 0)
            return -1;
        n = snprintf(dir, sizeof(dir), "%s/.local/state", home);
        if (n < 0 || (size_t)n >= sizeof(dir) || ensure_directory(dir) != 0)
            return -1;
    } else {
        int n = snprintf(path, size, "%s/fm/history", state);
        if (n < 0 || (size_t)n >= size) return -1;
        if (ensure_directory(state) != 0) return -1;
    }

    char dir[PATH_MAX];
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;
    size_t len = (size_t)(slash - path);
    if (len == 0 || len >= sizeof(dir)) return -1;
    memcpy(dir, path, len);
    dir[len] = '\0';
    return ensure_directory(dir);
}

int history_add(const char *command) {
    if (!command || !*command) return -1;

    char path[PATH_MAX];
    if (history_path(path, sizeof(path)) != 0) return -1;

    FILE *fp = fopen(path, "r");
    char *entries[HISTORY_LIMIT];
    size_t count = 0;
    if (fp) {
        char line[PATH_MAX];
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (!line[0]) continue;
            char *copy = strdup(line);
            if (!copy) break;
            if (count == HISTORY_LIMIT) {
                free(entries[0]);
                memmove(entries, entries + 1, (HISTORY_LIMIT - 1) * sizeof(entries[0]));
                --count;
            }
            entries[count++] = copy;
        }
        fclose(fp);
    }

    if (count == HISTORY_LIMIT) {
        free(entries[0]);
        memmove(entries, entries + 1, (HISTORY_LIMIT - 1) * sizeof(entries[0]));
        --count;
    }

    entries[count] = strdup(command);
    if (!entries[count]) {
        for (size_t i = 0; i < count; ++i) free(entries[i]);
        return -1;
    }
    ++count;

    char temp_path[PATH_MAX];
    int n = snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(temp_path)) {
        for (size_t i = 0; i < count; ++i) free(entries[i]);
        return -1;
    }

    int fd = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        for (size_t i = 0; i < count; ++i) free(entries[i]);
        return -1;
    }

    FILE *out = fdopen(fd, "w");
    if (!out) {
        close(fd);
        unlink(temp_path);
        for (size_t i = 0; i < count; ++i) free(entries[i]);
        return -1;
    }

    int ok = 1;
    for (size_t i = 0; i < count; ++i) {
        if (fprintf(out, "%s\n", entries[i]) < 0) {
            ok = 0;
            break;
        }
    }
    if (fclose(out) != 0) ok = 0;

    if (ok && rename(temp_path, path) != 0) ok = 0;
    if (!ok) unlink(temp_path);

    for (size_t i = 0; i < count; ++i) free(entries[i]);
    return ok ? 0 : -1;
}

char *history_menu(void) {
    char path[PATH_MAX];
    if (history_path(path, sizeof(path)) != 0) return NULL;

    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    size_t capacity = 256;
    size_t length = 0;
    char *menu = malloc(capacity);
    if (!menu) {
        fclose(fp);
        return NULL;
    }

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;

        size_t line_len = strlen(line);
        if (length + line_len + 2 > capacity) {
            size_t new_capacity = capacity;
            while (length + line_len + 2 > new_capacity)
                new_capacity *= 2;
            char *grown = realloc(menu, new_capacity);
            if (!grown) {
                free(menu);
                fclose(fp);
                return NULL;
            }
            menu = grown;
            capacity = new_capacity;
        }
        memcpy(menu + length, line, line_len);
        length += line_len;
        menu[length++] = '\n';
    }
    fclose(fp);

    if (length == 0) {
        free(menu);
        return NULL;
    }
    menu[length] = '\0';
    return menu;
}
