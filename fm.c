#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include "filelist.h"
#include "util.h"
#include "preview.h"
#include "ui.h"
#include "commands.h"\n#include "history.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <fontconfig/fontconfig.h>

#define BG_R 223
#define BG_G 191
#define BG_B 191
#define TEXT_R 0
#define TEXT_G 0
#define TEXT_B 0

#define PANE_RATIO 0.4
#define MARGIN 10
#define LINE_HEIGHT 24
#define INFO_HEIGHT 28
#define PATH_HEIGHT 28

#ifndef ENABLE_PREVIEW_IMAGE
#define ENABLE_PREVIEW_IMAGE 1
#endif
#ifndef ENABLE_PREVIEW_TEXT
#define ENABLE_PREVIEW_TEXT 1
#endif
#ifndef ENABLE_PREVIEW_HTML
#define ENABLE_PREVIEW_HTML 1
#endif
#ifndef ENABLE_PREVIEW_PDF
#define ENABLE_PREVIEW_PDF 1
#endif
#ifndef ENABLE_PREVIEW_MP3
#define ENABLE_PREVIEW_MP3 1
#endif
#ifndef ENABLE_PREVIEW_MEDIA
#define ENABLE_PREVIEW_MEDIA 1
#endif


Display *dpy;
Window win;
int screen;
char status_message[256] = {0};

FileList file_list;
FileList preview_list;
int preview_is_dir;
GdkPixbuf *preview_image;
int preview_is_image;
char *preview_html_text;
int preview_is_html;
char *preview_pdf_text;
int preview_is_pdf;
char *preview_media_text;
int preview_is_media;
char *preview_text_content;
int preview_is_text;

// Preview loading runs on a dedicated worker so the X11 event loop stays responsive.
PreviewResult preview_result = {0, PREVIEW_RESULT_NONE, NULL, NULL, {NULL, 0, 0, 0, NULL}, 0};
int preview_result_ready = 0;
int preview_wake_pipe[2] = {-1, -1};
unsigned long preview_generation = 0;
int preview_worker_started = 0;

Time last_click_time = 0;
int last_click_index = -1;


static int remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;

    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return unlink(path);
    }

    DIR *dir = opendir(path);
    if (!dir) return -1;

    const struct dirent *de;
    int result = 0;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char child[PATH_MAX];
        int n = snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(child) || remove_tree(child) != 0) {
            result = -1;
            break;
        }
    }
    closedir(dir);

    if (result == 0 && rmdir(path) != 0)
        result = -1;
    return result;
}

static int run_trash_command(const char *path) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execlp("trash", "trash", path, (char *)NULL);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void refresh_after_file_change(int old_selected) {
    // load_directory() frees list->path, so do not pass file_list.path
    // directly to it: make a private copy first.
    char current_path[PATH_MAX];
    snprintf(current_path, sizeof(current_path), "%s",
             file_list.path ? file_list.path : ".");

    clear_preview_state();
    load_directory(&file_list, current_path);
    if (file_list.count > 0) {
        file_list.selected = old_selected < file_list.count
            ? old_selected : file_list.count - 1;
    } else {
        file_list.selected = 0;
    }
    request_preview();
}

static void delete_selected_file(void) {
    if (file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (strcmp(entry->name, "..") == 0) return;

    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", file_list.path, entry->name);
    if (n < 0 || (size_t)n >= sizeof(path)) return;

    int old_selected = file_list.selected;
    if (remove_tree(path) == 0) {
        refresh_after_file_change(old_selected);
    }
}

static void trash_selected_file(void) {
    if (file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (strcmp(entry->name, "..") == 0) return;

    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", file_list.path, entry->name);
    if (n < 0 || (size_t)n >= sizeof(path)) return;

    int old_selected = file_list.selected;
    if (run_trash_command(path) == 0) {
        refresh_after_file_change(old_selected);
    }
}

static int parse_dmenu_argv(char ***argv_out, size_t *argc_out) {
    const char *config = getenv("DMENU");
    if (!config || !*config) config = "dmenu";

    size_t capacity = 8;
    size_t argc = 0;
    char **argv = calloc(capacity, sizeof(*argv));
    if (!argv) return -1;

    size_t token_capacity = 64;
    size_t token_len = 0;
    char *token = malloc(token_capacity);
    if (!token) {
        free(argv);
        return -1;
    }

    int in_single_quote = 0;
    int in_double_quote = 0;
    int escaped = 0;
    int token_started = 0;

    for (const char *p = config;; ++p) {
        unsigned char ch = (unsigned char)*p;
        int end_of_token = (ch == '\0');

        if (escaped) {
            if (end_of_token) {
                free(token);
                for (size_t i = 0; i < argc; ++i) free(argv[i]);
                free(argv);
                return -1;
            }
            if (token_len + 1 >= token_capacity) {
                token_capacity *= 2;
                char *grown = realloc(token, token_capacity);
                if (!grown) {
                    free(token);
                    for (size_t i = 0; i < argc; ++i) free(argv[i]);
                    free(argv);
                    return -1;
                }
                token = grown;
            }
            token[token_len++] = (char)ch;
            token_started = 1;
            escaped = 0;
            continue;
        }

        if (in_single_quote) {
            if (ch == '\0') {
                free(token);
                for (size_t i = 0; i < argc; ++i) free(argv[i]);
                free(argv);
                return -1;
            }
            if (ch == '\'') {
                in_single_quote = 0;
            } else {
                if (token_len + 1 >= token_capacity) {
                    token_capacity *= 2;
                    char *grown = realloc(token, token_capacity);
                    if (!grown) {
                        free(token);
                        for (size_t i = 0; i < argc; ++i) free(argv[i]);
                        free(argv);
                        return -1;
                    }
                    token = grown;
                }
                token[token_len++] = (char)ch;
                token_started = 1;
            }
            continue;
        }

        if (in_double_quote) {
            if (ch == '\0') {
                free(token);
                for (size_t i = 0; i < argc; ++i) free(argv[i]);
                free(argv);
                return -1;
            }
            if (ch == '"') {
                in_double_quote = 0;
            } else if (ch == '\\') {
                escaped = 1;
            } else {
                if (token_len + 1 >= token_capacity) {
                    token_capacity *= 2;
                    char *grown = realloc(token, token_capacity);
                    if (!grown) {
                        free(token);
                        for (size_t i = 0; i < argc; ++i) free(argv[i]);
                        free(argv);
                        return -1;
                    }
                    token = grown;
                }
                token[token_len++] = (char)ch;
                token_started = 1;
            }
            continue;
        }

        if (end_of_token || ch == ' ' || ch == '\t' || ch == '\n') {
            if (token_started) {
                if (argc + 1 >= capacity) {
                    capacity *= 2;
                    char **grown = realloc(argv, capacity * sizeof(*argv));
                    if (!grown) {
                        free(token);
                        for (size_t i = 0; i < argc; ++i) free(argv[i]);
                        free(argv);
                        return -1;
                    }
                    argv = grown;
                }
                token[token_len] = '\0';
                argv[argc] = strdup(token);
                if (!argv[argc]) {
                    free(token);
                    for (size_t i = 0; i < argc; ++i) free(argv[i]);
                    free(argv);
                    return -1;
                }
                ++argc;
                token_len = 0;
                token_started = 0;
            }
            if (end_of_token) break;
            continue;
        }

        if (ch == '\'') {
            in_single_quote = 1;
            token_started = 1;
        } else if (ch == '"') {
            in_double_quote = 1;
            token_started = 1;
        } else if (ch == '\\') {
            escaped = 1;
            token_started = 1;
        } else {
            if (token_len + 1 >= token_capacity) {
                token_capacity *= 2;
                char *grown = realloc(token, token_capacity);
                if (!grown) {
                    free(token);
                    for (size_t i = 0; i < argc; ++i) free(argv[i]);
                    free(argv);
                    return -1;
                }
                token = grown;
            }
            token[token_len++] = (char)ch;
            token_started = 1;
        }
    }

    free(token);

    char window_id[32];
    snprintf(window_id, sizeof(window_id), "%lu", (unsigned long)win);

    if (argc + 3 > capacity) {
        char **grown = realloc(argv, (argc + 3) * sizeof(*argv));
        if (!grown) {
            for (size_t i = 0; i < argc; ++i) free(argv[i]);
            free(argv);
            return -1;
        }
        argv = grown;
    }

    argv[argc] = strdup("-w");
    argv[argc + 1] = strdup(window_id);
    argv[argc + 2] = NULL;
    if (!argv[argc] || !argv[argc + 1]) {
        free(argv[argc]);
        free(argv[argc + 1]);
        for (size_t i = 0; i < argc; ++i) free(argv[i]);
        free(argv);
        return -1;
    }

    *argv_out = argv;
    *argc_out = argc + 2;
    return 0;
}

static void free_dmenu_argv(char **argv, size_t argc) {
    if (!argv) return;
    for (size_t i = 0; i < argc; ++i) free(argv[i]);
    free(argv);
}

static char *run_dmenu(const char *input) {
    char **argv = NULL;
    size_t argc = 0;
    if (parse_dmenu_argv(&argv, &argc) != 0) return NULL;

    int input_pipe[2] = {-1, -1};
    int output_pipe[2] = {-1, -1};
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        if (input_pipe[1] >= 0) close(input_pipe[1]);
        if (output_pipe[0] >= 0) close(output_pipe[0]);
        if (output_pipe[1] >= 0) close(output_pipe[1]);
        free_dmenu_argv(argv, argc);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        free_dmenu_argv(argv, argc);
        return NULL;
    }

    if (pid == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);

    size_t input_len = input ? strlen(input) : 0;
    size_t written = 0;
    while (written < input_len) {
        ssize_t n = write(input_pipe[1], input + written, input_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += (size_t)n;
    }
    close(input_pipe[1]);

    char result[PATH_MAX];
    size_t result_len = 0;
    while (result_len + 1 < sizeof(result)) {
        ssize_t n = read(output_pipe[0], result + result_len,
                         sizeof(result) - result_len - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        result_len += (size_t)n;
    }
    close(output_pipe[0]);

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) break;
    }
    free_dmenu_argv(argv, argc);

    if (result_len == 0) return NULL;
    result[result_len] = '\0';
    result[strcspn(result, "\r\n")] = '\0';
    if (result[0] == '\0') return NULL;
    return strdup(result);
}

typedef void (*InternalCommandHandler)(void);

typedef struct {
    const char *name;
    const char *key;
    InternalCommandHandler handler;
} InternalCommand;

static void open_selected_file(void);
static void command_rename(void);
static void command_search(void);
static void command_parent(void);
static void command_enter(void);
static void command_history(void);

static const InternalCommand internal_commands[] = {
    {"open", "o", open_selected_file},
    {"rename", "r", command_rename},
    {"delete", NULL, delete_selected_file},
    {"trash", NULL, trash_selected_file},
    {"search", "/", command_search},
    {"parent", "h", command_parent},
    {"enter", "l", command_enter},
    {"history", NULL, command_history}
};

#define INTERNAL_COMMAND_COUNT \
    (sizeof(internal_commands) / sizeof(internal_commands[0]))

static const ExternalCommand *find_external_command(const char *name) {
    for (size_t i = 0; external_commands[i].name != NULL; ++i) {
        if (external_commands[i].name &&
            strcmp(external_commands[i].name, name) == 0) {
            return &external_commands[i];
        }
    }
    return NULL;
}

static int command_key_matches(const char *key, KeySym ks,
                                   unsigned int state,
                                   const char *input, int input_len) {
    if (!key) return 0;

    if (strncmp(key, "C-", 2) == 0 &&
        key[2] != '\0' && key[3] == '\0') {
        unsigned char ch = (unsigned char)key[2];
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
        if ((state & ControlMask) == 0) return 0;

        /*
         * XLookupString translates some control combinations to a different
         * KeySym (C-i becomes Tab, C-m becomes Return, etc.). Compare the
         * translated input byte as well as the KeySym so config entries such
         * as "C-i" work consistently.
         */
        if (input && input_len > 0 &&
            (unsigned char)input[0] == (unsigned char)(ch & 0x1f)) {
            return 1;
        }

        KeySym expected = (KeySym)ch;
        return ks == expected || ks == (KeySym)(ch + ('a' - 'A'));
    }

    if (strcmp(key, "Del") == 0) return ks == XK_Delete;
    if (strcmp(key, "BackSpace") == 0) return ks == XK_BackSpace;

    return key[0] != '\0' && key[1] == '\0' &&
           ks == (KeySym)(unsigned char)key[0];
}

static void run_external_command(const ExternalCommand *command) {
    if (!command || !command->command || file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s",
                     file_list.path ? file_list.path : ".",
                     entry->name);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        set_status("Command path is too long");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        set_status("Could not start command");
        return;
    }
    if (pid == 0) {
        execlp(command->command, command->command, path, (char *)NULL);
        _exit(127);
    }

    set_status(command->name ? command->name : "Command started");
}

static const InternalCommand *find_internal_command(const char *name) {
    for (size_t i = 0; i < INTERNAL_COMMAND_COUNT; ++i) {
        if (strcmp(internal_commands[i].name, name) == 0) {
            return &internal_commands[i];
        }
    }
    return NULL;
}

static void command_history(void) {
    char *menu = history_menu();
    if (!menu) return;

    char *selection = run_dmenu(menu);
    free(menu);
    if (!selection) return;

    run_command(selection);
    free(selection);
}

static void run_command(const char *name) {
    const InternalCommand *internal = find_internal_command(name);
    const ExternalCommand *external = find_external_command(name);

    if (!internal && !external) return;

    history_add(name);

    if (internal) {
        internal->handler();
        return;
    }

    run_external_command(external);
}

static void show_command_menu(void) {
    size_t capacity = 1;
    for (size_t i = 0; i < INTERNAL_COMMAND_COUNT; ++i) {
        capacity += strlen(internal_commands[i].name) + 1;
    }
    for (size_t i = 0; external_commands[i].name != NULL; ++i) {
        if (external_commands[i].name) {
            capacity += strlen(external_commands[i].name) + 1;
        }
    }

    char *menu = calloc(capacity, 1);
    if (!menu) return;

    size_t offset = 0;
    for (size_t i = 0; i < INTERNAL_COMMAND_COUNT; ++i) {
        size_t len = strlen(internal_commands[i].name);
        memcpy(menu + offset, internal_commands[i].name, len);
        offset += len;
        menu[offset++] = '\n';
    }
    for (size_t i = 0; external_commands[i].name != NULL; ++i) {
        if (!external_commands[i].name) continue;
        size_t len = strlen(external_commands[i].name);
        memcpy(menu + offset, external_commands[i].name, len);
        offset += len;
        menu[offset++] = '\n';
    }
    menu[offset] = '\0';

    char *selection = run_dmenu(menu);
    free(menu);
    if (!selection) return;

    run_command(selection);
    free(selection);
}

static void open_file_with_xdg(const char *path) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
        _exit(127);
    }
}

void handle_mouse_button(const XButtonEvent *ev, int win_width, int win_height) {
    if (ev->button != Button1 || ev->x < 0 || ev->x >= win_width) return;

    int left_width = (int)(win_width * PANE_RATIO);
    if (ev->x >= left_width || ev->y < PATH_HEIGHT) return;

    int list_y = ev->y - PATH_HEIGHT;
    int visible_items = (win_height - PATH_HEIGHT) / LINE_HEIGHT;
    if (visible_items <= 0 || file_list.count <= 0) return;

    int scroll_offset = 0;
    if (file_list.selected >= visible_items) {
        scroll_offset = file_list.selected - visible_items + 1;
    }

    int row = list_y / LINE_HEIGHT;
    int index = scroll_offset + row;
    if (row < 0 || row >= visible_items || index < 0 || index >= file_list.count) return;

    file_list.selected = index;
    if (strcmp(file_list.entries[index].name, "..") != 0) {
        request_preview();
    }

    if (last_click_index == index && ev->time - last_click_time < 400) {
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s",
                         file_list.path ? file_list.path : ".",
                         file_list.entries[index].name);
        if (n >= 0 && (size_t)n < sizeof(path)) {
            if (file_list.entries[index].is_dir) {
                load_directory(&file_list, path);
                if (strcmp(file_list.entries[file_list.selected].name, "..") != 0) {
                    request_preview();
                }
            } else {
                open_file_with_xdg(path);
            }
        }
        last_click_index = -1;
    } else {
        last_click_index = index;
        last_click_time = ev->time;
    }
}

static int valid_new_name(const char *name) {
    if (!name || name[0] == '\0') return 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p == '/' || *p < 0x20 || *p == 0x7f) return 0;
    }
    return 1;
}

static void command_rename(void) {
    if (file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (strcmp(entry->name, "..") == 0) return;

    char *new_name = run_dmenu(entry->name);
    if (!new_name || !valid_new_name(new_name)) {
        free(new_name);
        return;
    }

    if (strcmp(entry->name, new_name) == 0) {
        free(new_name);
        return;
    }

    char old_path[PATH_MAX], new_path[PATH_MAX];
    int n = snprintf(old_path, sizeof(old_path), "%s/%s", file_list.path, entry->name);
    if (n < 0 || (size_t)n >= sizeof(old_path)) {
        free(new_name);
        return;
    }
    n = snprintf(new_path, sizeof(new_path), "%s/%s", file_list.path, new_name);
    if (n < 0 || (size_t)n >= sizeof(new_path)) {
        free(new_name);
        return;
    }

    if (rename(old_path, new_path) == 0) {
        char current_path[PATH_MAX];
        int path_len = snprintf(current_path, sizeof(current_path), "%s", file_list.path);
        if (path_len >= 0 && (size_t)path_len < sizeof(current_path)) {
            load_directory(&file_list, current_path);
            file_list.selected = 0;
            for (int i = 0; i < file_list.count; ++i) {
                if (strcmp(file_list.entries[i].name, new_name) == 0) {
                    file_list.selected = i;
                    break;
                }
            }
            if (file_list.count > 0) request_preview();
        }
    }

    free(new_name);
}

static void command_search(void) {
    if (file_list.count <= 0) return;

    size_t capacity = 1;
    for (int i = 0; i < file_list.count; ++i) {
        capacity += strlen(file_list.entries[i].name) + 1;
    }

    char *menu = calloc(capacity, 1);
    if (!menu) return;

    size_t offset = 0;
    for (int i = 0; i < file_list.count; ++i) {
        size_t len = strlen(file_list.entries[i].name);
        memcpy(menu + offset, file_list.entries[i].name, len);
        offset += len;
        menu[offset++] = '\n';
    }
    menu[offset] = '\0';

    char *selection = run_dmenu(menu);
    free(menu);
    if (!selection) return;

    for (int i = 0; i < file_list.count; ++i) {
        if (strcmp(file_list.entries[i].name, selection) == 0) {
            file_list.selected = i;
            request_preview();
            break;
        }
    }

    free(selection);
}

static void command_parent(void) {
    if (strcmp(file_list.path, ".") == 0) {
        load_directory(&file_list, "..");
    } else if (strcmp(file_list.path, "/") != 0) {
        const char *last_slash = strrchr(file_list.path, '/');
        char parent_path[PATH_MAX];
        if (last_slash && last_slash > file_list.path) {
            size_t len = (size_t)(last_slash - file_list.path);
            if (len >= sizeof(parent_path)) return;
            memcpy(parent_path, file_list.path, len);
            parent_path[len] = '\0';
        } else {
            snprintf(parent_path, sizeof(parent_path), ".");
        }
        load_directory(&file_list, parent_path);
    }
    request_preview();
}

static void command_enter(void) {
    if (file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (entry->is_dir) {
        char new_path[PATH_MAX];
        int n = snprintf(new_path, sizeof(new_path), "%s/%s",
                         file_list.path, entry->name);
        if (n >= 0 && (size_t)n < sizeof(new_path) &&
            strcmp(entry->name, "..") != 0) {
            load_directory(&file_list, new_path);
            request_preview();
        }
    } else {
        open_selected_file();
    }
}

static void open_selected_file(void) {
    if (file_list.count <= 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (entry->is_dir || strcmp(entry->name, "..") == 0) return;

    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s",
                     file_list.path, entry->name);
    if (n < 0 || (size_t)n >= sizeof(path)) return;

    open_file_with_xdg(path);
}

void handle_key(XKeyEvent *ev) {
    char input[32];
    KeySym ks;
    int input_len = XLookupString(ev, input, sizeof(input), &ks, NULL);
    (void)input_len;


    if (ks == XK_j || ks == XK_k) {
        /* j/k remain direct navigation keys rather than command-menu actions. */
    } else {
        for (size_t i = 0; i < INTERNAL_COMMAND_COUNT; ++i) {
            if (command_key_matches(internal_commands[i].key, ks, ev->state, input, input_len)) {
                internal_commands[i].handler();
                return;
            }
        }
        for (size_t i = 0; external_commands[i].name != NULL; ++i) {
            if (command_key_matches(external_commands[i].key, ks, ev->state, input, input_len)) {
                run_external_command(&external_commands[i]);
                return;
            }
        }
    }

    switch (ks) {
        case XK_colon:
            show_command_menu();
            break;
        case XK_j:
            if (file_list.count > 0) {
                int next = ui_next_search_match(file_list.selected, 1);
                if (next >= 0) {
                    file_list.selected = next;
                    request_preview();
                }
            }
            break;
        case XK_k:
            if (file_list.count > 0) {
                int next = ui_next_search_match(file_list.selected, -1);
                if (next >= 0) {
                    file_list.selected = next;
                    request_preview();
                }
            }
            break;
        /* h/l are handled by the unified command dispatcher. */
        case XK_q:
        case XK_Escape:
            exit(0);
            break;
    }
}

int main() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    screen = DefaultScreen(dpy);

    int win_width = 800;
    int win_height = 600;

    // Match the X11 window background to Cairo's first painted frame. This
    // prevents a white flash while an Expose event is waiting to be redrawn.
    Colormap colormap = DefaultColormap(dpy, screen);
    XColor bg_color;
    bg_color.red = (unsigned short)(BG_R * 65535 / 255);
    bg_color.green = (unsigned short)(BG_G * 65535 / 255);
    bg_color.blue = (unsigned short)(BG_B * 65535 / 255);
    bg_color.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, colormap, &bg_color)) {
        bg_color.pixel = WhitePixel(dpy, screen);
    }

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0,
                               win_width, win_height, 0,
                               BlackPixel(dpy, screen), bg_color.pixel);
    XStoreName(dpy, win, "File Manager");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    init_file_list(&file_list, ".");
    load_directory(&file_list, ".");
    init_file_list(&preview_list, ".");
    preview_is_dir = 0;

    // Check tool availability once at startup.
    check_tool_availability();

    Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    if (!start_preview_worker()) {
        fprintf(stderr, "Warning: could not start preview worker; previews disabled\n");
    }

    request_preview();

    int running = 1;
    int x_fd = ConnectionNumber(dpy);

    while (running) {
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);

            switch (ev.type) {
                case Expose:
                    if (ev.xexpose.count == 0) {
                        Window root;
                        int x, y;
                        unsigned int width, height, border, depth;
                        XGetGeometry(dpy, win, &root, &x, &y, &width, &height, &border, &depth);
                        draw_ui(width, height);
                    }
                    break;

                case ButtonPress:
                    handle_mouse_button(&ev.xbutton, win_width, win_height);
                    {
                        Window root;
                        int x, y;
                        unsigned int width, height, border, depth;
                        if (XGetGeometry(dpy, win, &root, &x, &y, &width, &height, &border, &depth)) {
                            draw_ui(width, height);
                        }
                    }
                    break;

                case KeyPress:
                    handle_key(&ev.xkey);
                    {
                        Window root;
                        int x, y;
                        unsigned int width, height, border, depth;
                        XGetGeometry(dpy, win, &root, &x, &y, &width, &height, &border, &depth);
                        draw_ui(width, height);
                    }
                    break;

                case ClientMessage:
                    if (ev.xclient.data.l[0] == (long)wm_delete_window) {
                        running = 0;
                    }
                    break;

                case ConfigureNotify:
                    draw_ui(ev.xconfigure.width, ev.xconfigure.height);
                    break;
            }

            if (!running) break;
        }

        if (!running) break;

        int preview_applied = 0;
        if (preview_wake_pipe[0] >= 0) {
            char buffer[64];
            ssize_t n;
            while ((n = read(preview_wake_pipe[0], buffer, sizeof(buffer))) > 0) {
                (void)n;
                apply_preview_result();
                preview_applied = 1;
            }
        }

        // The wake pipe may become readable between X event processing and
        // select(). If we consume it here, redraw immediately instead of
        // waiting for another X event (for example, a focus change).
        if (preview_applied) {
            Window root;
            int x, y;
            unsigned int width, height, border, depth;
            XGetGeometry(dpy, win, &root, &x, &y, &width, &height, &border, &depth);
            draw_ui(width, height);
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(x_fd, &readfds);
        int max_fd = x_fd;

        if (preview_wake_pipe[0] >= 0) {
            FD_SET(preview_wake_pipe[0], &readfds);
            if (preview_wake_pipe[0] > max_fd) max_fd = preview_wake_pipe[0];
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (preview_wake_pipe[0] >= 0 && FD_ISSET(preview_wake_pipe[0], &readfds)) {
            char buffer[64];
            while (read(preview_wake_pipe[0], buffer, sizeof(buffer)) > 0) {
                apply_preview_result();
            }

            Window root;
            int x, y;
            unsigned int width, height, border, depth;
            XGetGeometry(dpy, win, &root, &x, &y, &width, &height, &border, &depth);
            draw_ui(width, height);
        }
    }

    stop_preview_worker();

    free_file_list(&file_list);
    free_file_list(&preview_list);
    free_preview_image();
    free_preview_html();
    free_preview_pdf();
    free_preview_text();
    free_preview_media();
    free_scaled_image_cache();
    free_pango_objects();
    free_draw_surfaces();
    FcFini();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
