#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>

#define SEPIA_R 240
#define SEPIA_G 220
#define SEPIA_B 190
#define TEXT_R 0
#define TEXT_G 0
#define TEXT_B 0

#define PANE_RATIO 0.4
#define MARGIN 10
#define LINE_HEIGHT 24

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

Display *dpy;
Window win;
int screen;

FileList file_list;

void init_file_list() {
    file_list.entries = NULL;
    file_list.count = 0;
    file_list.capacity = 0;
    file_list.selected = 0;
    file_list.path = strdup(".");
}

void free_file_list() {
    for (int i = 0; i < file_list.count; i++) {
        free(file_list.entries[i].name);
    }
    free(file_list.entries);
    free(file_list.path);
}

int compare_entries(const void *a, const void *b) {
    FileEntry *ea = (FileEntry *)a;
    FileEntry *eb = (FileEntry *)b;
    // Directories first, then alphabetical
    if (ea->is_dir != eb->is_dir) {
        return eb->is_dir - ea->is_dir;
    }
    return strcmp(ea->name, eb->name);
}

void load_directory(const char *path) {
    DIR *dir;
    struct dirent *ent;
    struct stat st;

    // Free old entries
    for (int i = 0; i < file_list.count; i++) {
        free(file_list.entries[i].name);
    }
    free(file_list.entries);
    free(file_list.path);

    file_list.entries = NULL;
    file_list.count = 0;
    file_list.capacity = 0;
    file_list.selected = 0;
    file_list.path = strdup(path);

    if ((dir = opendir(path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            char fullpath[4096];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);
            
            if (stat(fullpath, &st) == 0) {
                if (file_list.count >= file_list.capacity) {
                    file_list.capacity = file_list.capacity == 0 ? 16 : file_list.capacity * 2;
                    file_list.entries = realloc(file_list.entries, file_list.capacity * sizeof(FileEntry));
                }
                file_list.entries[file_list.count].name = strdup(ent->d_name);
                file_list.entries[file_list.count].is_dir = S_ISDIR(st.st_mode);
                file_list.count++;
            }
        }
        closedir(dir);
    }

    qsort(file_list.entries, file_list.count, sizeof(FileEntry), compare_entries);
}

void draw_text(cairo_t *cr, const char *text, int x, int y, int width, PangoLayout *layout) {
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

void draw_file_list(cairo_t *cr, int x, int y, int width, int height) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string("Sans 12");
    pango_layout_set_font_description(layout, desc);

    cairo_set_source_rgb(cr, SEPIA_R/255.0, SEPIA_G/255.0, SEPIA_B/255.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    int scroll_offset = 0;
    int visible_items = height / LINE_HEIGHT;
    if (file_list.selected >= visible_items) {
        scroll_offset = file_list.selected - visible_items + 1;
    }

    for (int i = scroll_offset; i < file_list.count && i < scroll_offset + visible_items; i++) {
        int item_y = y + (i - scroll_offset) * LINE_HEIGHT + MARGIN;

        if (i == file_list.selected) {
            cairo_set_source_rgb(cr, 200/255.0, 200/255.0, 200/255.0);
            cairo_rectangle(cr, x, item_y - MARGIN/2, width, LINE_HEIGHT);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
        
        char display_name[256];
        if (file_list.entries[i].is_dir) {
            snprintf(display_name, sizeof(display_name), "%s/", file_list.entries[i].name);
        } else {
            strncpy(display_name, file_list.entries[i].name, sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';
        }

        draw_text(cr, display_name, x + MARGIN, item_y, width - 2 * MARGIN, layout);
    }

    pango_font_description_free(desc);
    g_object_unref(layout);
}

void draw_preview(cairo_t *cr, int x, int y, int width, int height) {
    cairo_set_source_rgb(cr, SEPIA_R/255.0, SEPIA_G/255.0, SEPIA_B/255.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *desc = pango_font_description_from_string("Sans 12");
    pango_layout_set_font_description(layout, desc);

    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
    
    if (file_list.count > 0 && file_list.selected >= 0 && file_list.selected < file_list.count) {
        char preview_text[256];
        if (file_list.entries[file_list.selected].is_dir) {
            snprintf(preview_text, sizeof(preview_text), "Directory: %s", file_list.entries[file_list.selected].name);
        } else {
            snprintf(preview_text, sizeof(preview_text), "File: %s", file_list.entries[file_list.selected].name);
        }
        draw_text(cr, preview_text, x + MARGIN, y + MARGIN + LINE_HEIGHT, width - 2 * MARGIN, layout);
    } else {
        draw_text(cr, "No selection", x + MARGIN, y + MARGIN + LINE_HEIGHT, width - 2 * MARGIN, layout);
    }

    pango_font_description_free(desc);
    g_object_unref(layout);
}

void draw_ui(int win_width, int win_height) {
    int left_width = (int)(win_width * PANE_RATIO);
    int right_width = win_width - left_width;

    cairo_surface_t *surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_width, win_height);
    cairo_t *cr = cairo_create(surface);

    draw_file_list(cr, 0, 0, left_width, win_height);
    draw_preview(cr, left_width, 0, right_width, win_height);

    // Divider line
    cairo_set_source_rgb(cr, 150/255.0, 150/255.0, 150/255.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, left_width, 0);
    cairo_line_to(cr, left_width, win_height);
    cairo_stroke(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void handle_key(XKeyEvent *ev) {
    KeySym ks = XLookupKeysym(ev, 0);

    switch (ks) {
        case XK_j:
            if (file_list.selected < file_list.count - 1) {
                file_list.selected++;
            }
            break;
        case XK_k:
            if (file_list.selected > 0) {
                file_list.selected--;
            }
            break;
        case XK_l:
            if (file_list.count > 0 && file_list.entries[file_list.selected].is_dir) {
                char new_path[4096];
                snprintf(new_path, sizeof(new_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
                load_directory(new_path);
            }
            break;
        case XK_h:
            if (strcmp(file_list.path, ".") != 0) {
                // Go to parent directory
                char *last_slash = strrchr(file_list.path, '/');
                if (last_slash) {
                    char parent_path[4096];
                    if (last_slash == file_list.path) {
                        // Root directory
                        snprintf(parent_path, sizeof(parent_path), "/");
                    } else {
                        int len = last_slash - file_list.path;
                        strncpy(parent_path, file_list.path, len);
                        parent_path[len] = '\0';
                    }
                    load_directory(parent_path);
                } else {
                    load_directory(".");
                }
            }
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

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, win_width, win_height, 0, 0, 0xFFFFFFFF);
    XStoreName(dpy, win, "File Manager");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    init_file_list();
    load_directory(".");

    Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    int running = 1;
    while (running) {
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
    }

    free_file_list();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
