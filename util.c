#define _GNU_SOURCE
#include "util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

char *get_absolute_path(const char *path) {
    char buf[4096];
    if (realpath(path, buf)) {
        return strdup(buf);
    }
    return strdup(path);
}

char *get_display_path(const char *path) {
    char *abs_path = get_absolute_path(path);
    if (!abs_path) return NULL;

    const char *home = getenv("HOME");
    if (home) {
        size_t hlen = strlen(home);
        if (strncmp(abs_path, home, hlen) == 0 &&
            (abs_path[hlen] == '/' || abs_path[hlen] == '\0')) {
            size_t rest_len = strlen(abs_path) - hlen;
            char *result = malloc(rest_len + 2);
            if (!result) {
                free(abs_path);
                return NULL;
            }
            if (rest_len == 0) {
                snprintf(result, rest_len + 2, "~");
            } else {
                snprintf(result, rest_len + 2, "~%s", abs_path + hlen);
            }
            free(abs_path);
            return result;
        }
    }
    return abs_path;
}

void format_file_info(const char *path, const char *name, int is_dir,
                      char *buf, int buf_size) {
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(buf, buf_size, "? %s", name);
        return;
    }

    char perms[11];
    perms[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
    perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
    perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
    perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
    perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
    perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
    perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
    perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
    perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
    perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
    perms[10] = '\0';

    char size_str[32];
    if (is_dir) {
        snprintf(size_str, sizeof(size_str), "-");
    } else if (st.st_size < 1024) {
        snprintf(size_str, sizeof(size_str), "%ldB", st.st_size);
    } else if (st.st_size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1fK", st.st_size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.1fM",
                 st.st_size / (1024.0 * 1024.0));
    }

    const struct tm *tm = localtime(&st.st_mtime);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a %b %d %H:%M:%S %Y", tm);

    snprintf(buf, buf_size, "%s %ld %ld %s %s",
             perms, (long)st.st_uid, (long)st.st_gid, size_str, date_str);
}


int tool_lynx_available = 0;
int tool_pdfinfo_available = 0;
int tool_mediainfo_available = 0;
int tool_mp3info_available = 0;

int tool_is_available(const char *tool_name) {
    const char *path_env = getenv("PATH");
    if (!path_env) return 0;
    char *path_copy = strdup(path_env);
    if (!path_copy) return 0;
    int found = 0;
    const char *token = strtok(path_copy, ":");
    while (token) {
        char tool_path[4096];
        snprintf(tool_path, sizeof(tool_path), "%s/%s", token, tool_name);
        if (access(tool_path, X_OK) == 0) {
            found = 1;
            break;
        }
        token = strtok(NULL, ":");
    }
    free(path_copy);
    return found;
}

void check_tool_availability(void) {
    tool_lynx_available = tool_is_available("lynx");
    tool_pdfinfo_available = tool_is_available("pdfinfo");
    tool_mediainfo_available = tool_is_available("mediainfo");
    tool_mp3info_available = tool_is_available("mp3info");
}
