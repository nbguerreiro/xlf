#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include "filelist.h"
#include "util.h"
#include "preview.h"
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

#define SEARCH_MAX 256
int search_active = 0;
char search_query[SEARCH_MAX];
size_t search_query_len = 0;
int rename_active = 0;
char rename_query[SEARCH_MAX];
size_t rename_query_len = 0;

// Reused Cairo surfaces for the window and off-screen frame buffer.
cairo_surface_t *window_surface = NULL;
cairo_surface_t *backbuffer_surface = NULL;
int surface_width = 0;
int surface_height = 0;

// Cached Pango objects (reused across frames)
PangoLayout *layout_normal = NULL;
PangoLayout *layout_bold = NULL;
PangoLayout *layout_mono = NULL;
PangoLayout *layout_small = NULL;
PangoLayout *layout_path = NULL;
PangoFontDescription *desc_normal = NULL;
PangoFontDescription *desc_bold = NULL;
PangoFontDescription *desc_mono = NULL;
PangoFontDescription *desc_small = NULL;
PangoFontDescription *desc_path = NULL;

// Cached scaled image (reused across resize/redraw cycles)
ScaledImageCache scaled_image_cache = { NULL, 0, 0 };

pthread_t preview_thread;
pthread_mutex_t preview_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t preview_cond = PTHREAD_COND_INITIALIZER;
PreviewTask preview_task = {0, PREVIEW_RESULT_NONE, NULL};
int preview_task_pending = 0;
int preview_worker_stop = 0;


// Tool availability cache (checked once at startup)
int tool_lynx_available = 0;
int tool_pdfinfo_available = 0;
int tool_mediainfo_available = 0;
int tool_mp3info_available = 0;
char status_message[256] = "";

// Check if a tool is available in PATH
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

// Check all tools once at startup
void check_tool_availability() {
    tool_lynx_available = tool_is_available("lynx");
    tool_pdfinfo_available = tool_is_available("pdfinfo");
    tool_mediainfo_available = tool_is_available("mediainfo");
    tool_mp3info_available = tool_is_available("mp3info");
}

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

static void begin_rename(void) {
    if (file_list.count <= 0) return;
    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (strcmp(entry->name, "..") == 0) return;

    rename_active = 1;
    rename_query_len = strlen(entry->name);
    if (rename_query_len >= SEARCH_MAX) rename_query_len = SEARCH_MAX - 1;
    memcpy(rename_query, entry->name, rename_query_len);
    rename_query[rename_query_len] = '\0';
}

static void finish_rename(int accept) {
    if (!rename_active) return;
    rename_active = 0;
    if (!accept || rename_query_len == 0) return;

    const FileEntry *entry = &file_list.entries[file_list.selected];
    if (strcmp(entry->name, rename_query) == 0) return;

    for (const unsigned char *p = (const unsigned char *)rename_query; *p; ++p) {
        if (*p == '/' || *p < 0x20 || *p == 0x7f) return;
    }

    char old_path[PATH_MAX], new_path[PATH_MAX];
    int n = snprintf(old_path, sizeof(old_path), "%s/%s", file_list.path, entry->name);
    if (n < 0 || (size_t)n >= sizeof(old_path)) return;
    n = snprintf(new_path, sizeof(new_path), "%s/%s", file_list.path, rename_query);
    if (n < 0 || (size_t)n >= sizeof(new_path)) return;

    if (rename(old_path, new_path) == 0) {
        // load_directory() replaces list->path, so don't pass file_list.path
        // directly: it frees that string before duplicating the new path.
        char current_path[PATH_MAX];
        int path_len = snprintf(current_path, sizeof(current_path), "%s", file_list.path);
        if (path_len < 0 || (size_t)path_len >= sizeof(current_path)) return;

        load_directory(&file_list, current_path);

        // Keep the renamed entry selected so the preview and path bar update
        // to the new name immediately.
        file_list.selected = 0;
        for (int i = 0; i < file_list.count; ++i) {
            if (strcmp(file_list.entries[i].name, rename_query) == 0) {
                file_list.selected = i;
                break;
            }
        }
        if (file_list.count > 0) {
            request_preview();
        }
    }
}

static void handle_rename_key(XKeyEvent *ev, KeySym ks) {
    if (ks == XK_Escape) {
        finish_rename(0);
        return;
    }
    if (ks == XK_Return || ks == XK_KP_Enter) {
        finish_rename(1);
        return;
    }
    if (ks == XK_BackSpace) {
        if (rename_query_len > 0) rename_query[--rename_query_len] = '\0';
        return;
    }

    char input[SEARCH_MAX];
    KeySym translated;
    int n = XLookupString(ev, input, sizeof(input) - 1, &translated, NULL);
    if (n <= 0 || rename_query_len + (size_t)n >= SEARCH_MAX) return;

    for (int i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)input[i];
        if (ch < 0x20 || ch == 0x7f || ch == '/') return;
    }

    memcpy(rename_query + rename_query_len, input, (size_t)n);
    rename_query_len += (size_t)n;
    rename_query[rename_query_len] = '\0';
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

static void search_select(void) {
    if (!search_active || search_query_len == 0 || file_list.count <= 0) return;

    int start = file_list.selected;
    for (int offset = 1; offset <= file_list.count; ++offset) {
        int index = (start + offset) % file_list.count;
        if (strcasestr(file_list.entries[index].name, search_query) != NULL) {
            file_list.selected = index;
            request_preview();
            return;
        }
    }

    if (strcasestr(file_list.entries[start].name, search_query) != NULL) {
        request_preview();
    }
}

static void handle_search_key(XKeyEvent *ev, KeySym ks) {
    if (ks == XK_Escape || ks == XK_Return || ks == XK_KP_Enter) {
        search_active = 0;
        search_query_len = 0;
        search_query[0] = '\0';
        return;
    }

    if (ks == XK_BackSpace) {
        if (search_query_len > 0) {
            search_query[--search_query_len] = '\0';
            search_select();
        }
        return;
    }

    char input[8];
    KeySym translated;
    int n = XLookupString(ev, input, sizeof(input) - 1, &translated, NULL);
    if (n <= 0 || search_query_len + (size_t)n >= SEARCH_MAX) return;

    for (int i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)input[i];
        if (ch < 0x20 || ch == 0x7f) return;
    }

    memcpy(search_query + search_query_len, input, (size_t)n);
    search_query_len += (size_t)n;
    search_query[search_query_len] = '\0';
    search_select();
}

void handle_key(XKeyEvent *ev) {
    KeySym ks = XLookupKeysym(ev, 0);

    if (rename_active) {
        handle_rename_key(ev, ks);
        return;
    }

    if (search_active) {
        if (ks == XK_Up || ks == XK_Down) {
            int next = next_search_match(file_list.selected, ks == XK_Down ? 1 : -1);
            if (next >= 0) {
                file_list.selected = next;
                request_preview();
            }
            return;
        }
        handle_search_key(ev, ks);
        return;
    }

    switch (ks) {
        case XK_slash:
            search_active = 1;
            search_query_len = 0;
            search_query[0] = '\0';
            break;
        case XK_o:
            open_selected_file();
            break;
        case XK_r:
            begin_rename();
            break;
        case XK_Delete:
            delete_selected_file();
            break;
        case XK_BackSpace:
            trash_selected_file();
            break;
        case XK_j:
            if (file_list.count > 0) {
                int next = next_search_match(file_list.selected, 1);
                if (next >= 0) {
                    file_list.selected = next;
                    request_preview();
                }
            }
            break;
        case XK_k:
            if (file_list.count > 0) {
                int next = next_search_match(file_list.selected, -1);
                if (next >= 0) {
                    file_list.selected = next;
                    request_preview();
                }
            }
            break;
        case XK_l:
            if (file_list.count > 0) {
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
            break;
        case XK_h:
            // Go to parent directory
            if (strcmp(file_list.path, ".") == 0) {
                // Current directory is ".", go to ".."
                load_directory(&file_list, "..");
            } else if (strcmp(file_list.path, "/") == 0) {
                // Already at root, do nothing
                ;
            } else {
                const char *last_slash = strrchr(file_list.path, '/');
                char parent_path[4096];
                if (last_slash && last_slash > file_list.path) {
                    // Remove everything after last slash
                    int len = last_slash - file_list.path;
                    strncpy(parent_path, file_list.path, len);
                    parent_path[len] = '\0';
                } else {
                    // Path doesn't contain slash (shouldn't happen if not "." or "/")
                    snprintf(parent_path, sizeof(parent_path), ".");
                }
                load_directory(&file_list, parent_path);
            }
            request_preview();
            break;
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
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
