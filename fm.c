#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo-xlib.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

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

// Cached Pango objects (reused across frames)
PangoLayout *layout_normal = NULL;
PangoLayout *layout_bold = NULL;
PangoLayout *layout_mono = NULL;
PangoLayout *layout_small = NULL;
PangoFontDescription *desc_normal = NULL;
PangoFontDescription *desc_bold = NULL;
PangoFontDescription *desc_mono = NULL;
PangoFontDescription *desc_small = NULL;

void init_file_list(FileList *list, const char *path) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(path);
}

void free_file_list(FileList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        free(list->entries[i].name);
    }
    free(list->entries);
    free(list->path);
}

void free_preview_image() {
    if (preview_image) {
        g_object_unref(preview_image);
        preview_image = NULL;
    }
    preview_is_image = 0;
}

void free_preview_html() {
    if (preview_html_text) {
        free(preview_html_text);
        preview_html_text = NULL;
    }
    preview_is_html = 0;
}

void free_preview_pdf() {
    if (preview_pdf_text) {
        free(preview_pdf_text);
        preview_pdf_text = NULL;
    }
    preview_is_pdf = 0;
}

void free_preview_text() {
    if (preview_text_content) {
        free(preview_text_content);
        preview_text_content = NULL;
    }
    preview_is_text = 0;
}

void free_preview_media() {
    if (preview_media_text) {
        free(preview_media_text);
        preview_media_text = NULL;
    }
    preview_is_media = 0;
}

void init_pango_objects(cairo_t *cr) {
    if (!layout_normal) {
        layout_normal = pango_cairo_create_layout(cr);
    }
    if (!layout_bold) {
        layout_bold = pango_cairo_create_layout(cr);
    }
    if (!layout_mono) {
        layout_mono = pango_cairo_create_layout(cr);
    }
    if (!layout_small) {
        layout_small = pango_cairo_create_layout(cr);
    }
    
    if (!desc_normal) {
        desc_normal = pango_font_description_from_string("Sans 12");
    }
    if (!desc_bold) {
        desc_bold = pango_font_description_from_string("Sans Bold 12");
    }
    if (!desc_mono) {
        desc_mono = pango_font_description_from_string("Monospace 10");
    }
    if (!desc_small) {
        desc_small = pango_font_description_from_string("Sans 10");
    }
    
    if (layout_normal) {
        pango_layout_set_font_description(layout_normal, desc_normal);
    }
    if (layout_bold) {
        pango_layout_set_font_description(layout_bold, desc_bold);
    }
    if (layout_mono) {
        pango_layout_set_font_description(layout_mono, desc_mono);
    }
    if (layout_small) {
        pango_layout_set_font_description(layout_small, desc_small);
    }
}

void free_pango_objects() {
    if (layout_normal) {
        g_object_unref(layout_normal);
        layout_normal = NULL;
    }
    if (layout_bold) {
        g_object_unref(layout_bold);
        layout_bold = NULL;
    }
    if (layout_mono) {
        g_object_unref(layout_mono);
        layout_mono = NULL;
    }
    if (layout_small) {
        g_object_unref(layout_small);
        layout_small = NULL;
    }
    if (desc_normal) {
        pango_font_description_free(desc_normal);
        desc_normal = NULL;
    }
    if (desc_bold) {
        pango_font_description_free(desc_bold);
        desc_bold = NULL;
    }
    if (desc_mono) {
        pango_font_description_free(desc_mono);
        desc_mono = NULL;
    }
    if (desc_small) {
        pango_font_description_free(desc_small);
        desc_small = NULL;
    }
}

int is_image_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "jpg") == 0 ||
           strcasecmp(ext, "jpeg") == 0 ||
           strcasecmp(ext, "png") == 0 ||
           strcasecmp(ext, "gif") == 0 ||
           strcasecmp(ext, "bmp") == 0 ||
           strcasecmp(ext, "tiff") == 0;
}

int is_pdf_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "pdf") == 0;
}

int is_text_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "txt") == 0 ||
           strcasecmp(ext, "md") == 0 ||
           strcasecmp(ext, "csv") == 0 ||
           strcasecmp(ext, "log") == 0 ||
           strcasecmp(ext, "conf") == 0 ||
           strcasecmp(ext, "config") == 0 ||
           strcasecmp(ext, "json") == 0 ||
           strcasecmp(ext, "xml") == 0 ||
           strcasecmp(ext, "yml") == 0 ||
           strcasecmp(ext, "yaml") == 0 ||
           strcasecmp(ext, "sh") == 0 ||
           strcasecmp(ext, "py") == 0 ||
           strcasecmp(ext, "c") == 0 ||
           strcasecmp(ext, "h") == 0 ||
           strcasecmp(ext, "cpp") == 0 ||
           strcasecmp(ext, "cc") == 0 ||
           strcasecmp(ext, "hs") == 0 ||
           strcasecmp(ext, "rs") == 0 ||
           strcasecmp(ext, "js") == 0 ||
           strcasecmp(ext, "ts") == 0 ||
           strcasecmp(ext, "go") == 0 ||
           strcasecmp(ext, "rb") == 0 ||
           strcasecmp(ext, "php") == 0 ||
           strcasecmp(ext, "java") == 0 ||
           strcasecmp(ext, "lua") == 0 ||
           strcasecmp(ext, "sql") == 0;
}

int is_html_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "htm") == 0 ||
           strcasecmp(ext, "html") == 0 ||
           strcasecmp(ext, "xhtml") == 0;
}

int is_mp3_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "mp3") == 0;
}

int is_media_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    ext++;
    return strcasecmp(ext, "aac") == 0 ||
           strcasecmp(ext, "flac") == 0 ||
           strcasecmp(ext, "wav") == 0 ||
           strcasecmp(ext, "aif") == 0 ||
           strcasecmp(ext, "aiff") == 0 ||
           strcasecmp(ext, "avi") == 0 ||
           strcasecmp(ext, "mp4") == 0 ||
           strcasecmp(ext, "m4a") == 0 ||
           strcasecmp(ext, "m4b") == 0 ||
           strcasecmp(ext, "m4v") == 0 ||
           strcasecmp(ext, "mkv") == 0 ||
           strcasecmp(ext, "ogg") == 0 ||
           strcasecmp(ext, "opus") == 0 ||
           strcasecmp(ext, "ape") == 0 ||
           strcasecmp(ext, "webm") == 0 ||
           strcasecmp(ext, "mp3") == 0;
}

int is_small_image(const char *path, off_t max_size) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size <= max_size;
}

char *load_text_preview(const char *cmd, const char *arg1, const char *arg2, const char *path) {
    if (!cmd || !path) return NULL;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) _exit(127);
        close(pipefd[1]);
        if (arg1 && arg2) {
            execlp(cmd, cmd, arg1, arg2, path, (char *)NULL);
        } else if (arg1) {
            execlp(cmd, cmd, arg1, path, (char *)NULL);
        } else {
            execlp(cmd, cmd, path, (char *)NULL);
        }
        _exit(127);
    }

    // Parent process
    close(pipefd[1]);

    // Read output
    char *buffer = NULL;
    size_t buffer_size = 0;
    size_t total = 0;

    while (1) {
        if (total + 1 >= buffer_size) {
            size_t new_size = buffer_size == 0 ? 4096 : buffer_size * 2;
            char *tmp = realloc(buffer, new_size);
            if (!tmp) {
                free(buffer);
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return NULL;
            }
            buffer = tmp;
            buffer_size = new_size;
        }
        ssize_t n = read(pipefd[0], buffer + total, buffer_size - total - 1);
        if (n > 0) {
            total += n;
            continue;
        }
        if (n == 0) break;
        if (n == -1) {
            if (errno == EINTR) continue;
            free(buffer);
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return NULL;
        }
    }

    close(pipefd[0]);

    waitpid(pid, NULL, 0);

    if (total == 0) {
        free(buffer);
        return NULL;
    }

    buffer[total] = '\0';
    return buffer;
}

char *load_html_preview(const char *path) {
    return load_text_preview("lynx", "-force_html", "-dump", path);
}

char *load_text_content(const char *path, off_t max_size) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > max_size) {
        fclose(f);
        return NULL;
    }

    char *buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buffer, 1, fsize, f);
    fclose(f);

    if (n <= 0) {
        free(buffer);
        return NULL;
    }

    buffer[n] = '\0';
    return buffer;
}

char *load_pdf_preview(const char *path) {
    return load_text_preview("pdfinfo", NULL, NULL, path);
}

char *load_media_preview(const char *path) {
    return load_text_preview("mediainfo", NULL, NULL, path);
}

char *get_absolute_path(const char *path) {
    char buf[4096];
    if (realpath(path, buf)) {
        char *r = strdup(buf);
        return r;
    }
    return strdup(path);
}

char *get_display_path(const char *path) {
    char *abs_path = get_absolute_path(path);
    if (!abs_path) return NULL;
    char *home = getenv("HOME");
    if (home) {
        size_t hlen = strlen(home);
        if (strncmp(abs_path, home, hlen) == 0 && (abs_path[hlen] == '/' || abs_path[hlen] == '\0')) {
            size_t rest_len = strlen(abs_path) - hlen;
            // Allocate for '~' + rest + null
            char *result = malloc(rest_len + 2);
            if (!result) { free(abs_path); return NULL; }
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

void format_file_info(const char *path, const char *name, int is_dir, char *buf, int buf_size) {
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(buf, buf_size, "? %s", name);
        return;
    }

    // Format permissions
    char perms[11];
    perms[0] = (S_ISDIR(st.st_mode)) ? 'd' : '-';
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

    // Format size
    char size_str[32];
    if (is_dir) {
        snprintf(size_str, sizeof(size_str), "%s", "-");
    } else if (st.st_size < 1024) {
        snprintf(size_str, sizeof(size_str), "%ldB", st.st_size);
    } else if (st.st_size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1fK", st.st_size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.1fM", st.st_size / (1024.0 * 1024.0));
    }

    // Format date
    struct tm *tm = localtime(&st.st_mtime);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a %b %d %H:%M:%S %Y", tm);

    snprintf(buf, buf_size, "%s %ld %ld %s %s", perms, (long)st.st_uid, (long)st.st_gid, size_str, date_str);
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

void load_directory(FileList *list, const char *path) {
    DIR *dir;
    struct dirent *ent;
    struct stat st;

    // Free old entries
    for (int i = 0; i < list->count; i++) {
        free(list->entries[i].name);
    }
    free(list->entries);
    free(list->path);

    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
    list->selected = 0;
    list->path = strdup(path);
    if (!list->path) list->path = NULL;

    if ((dir = opendir(path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            char fullpath[4096];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);
            
            if (stat(fullpath, &st) == 0) {
                if (list->count >= list->capacity) {
                    size_t newcap = list->capacity == 0 ? 16 : list->capacity * 2;
                    FileEntry *tmp = realloc(list->entries, newcap * sizeof(FileEntry));
                    if (!tmp) {
                        // allocation failed: stop adding further entries
                        break;
                    }
                    list->entries = tmp;
                    list->capacity = newcap;
                }
                char *name = strdup(ent->d_name);
                if (!name) continue;
                list->entries[list->count].name = name;
                list->entries[list->count].is_dir = S_ISDIR(st.st_mode);
                list->count++;
            }
        }
        closedir(dir);
    }

    if (list->entries && list->count > 0) {
        qsort(list->entries, list->count, sizeof(FileEntry), compare_entries);
    }
}

void draw_text(cairo_t *cr, const char *text, int x, int y, int width, PangoLayout *layout) {
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

void draw_path_bar(cairo_t *cr, int x, int y, int width, FileList *list) {
    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, y, width, PATH_HEIGHT);
    cairo_fill(cr);

    // Draw a separator line at bottom of path bar
    cairo_set_source_rgb(cr, 150/255.0, 150/255.0, 150/255.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x, y + PATH_HEIGHT);
    cairo_line_to(cr, x + width, y + PATH_HEIGHT);
    cairo_stroke(cr);

    char full_path[4096];
    if (list->count > 0 && list->selected >= 0 && list->selected < list->count) {
        snprintf(full_path, sizeof(full_path), "%s/%s", list->path, list->entries[list->selected].name);
    } else {
        strncpy(full_path, list->path ? list->path : ".", sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }
    
    char *display_path = get_display_path(full_path);

    PangoLayout *temp_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *temp_desc = pango_font_description_from_string("Sans 11");
    pango_layout_set_font_description(temp_layout, temp_desc);
    pango_layout_set_ellipsize(temp_layout, PANGO_ELLIPSIZE_MIDDLE);
    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
    
    cairo_move_to(cr, x + MARGIN, (PATH_HEIGHT / 2) - 5);
    pango_layout_set_text(temp_layout, display_path ? display_path : "", -1);
    pango_cairo_show_layout(cr, temp_layout);

    pango_font_description_free(temp_desc);
    g_object_unref(temp_layout);
    free(display_path);
}

void draw_file_entries(cairo_t *cr, FileList *list, int x, int y, int width, int height) {
    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    int scroll_offset = 0;
    int visible_items = height / LINE_HEIGHT;
    if (list->selected >= visible_items) {
        scroll_offset = list->selected - visible_items + 1;
    }

    for (int i = scroll_offset; i < list->count && i < scroll_offset + visible_items; i++) {
        int item_y = y + (i - scroll_offset) * LINE_HEIGHT + MARGIN;

        if (i == list->selected) {
            cairo_set_source_rgb(cr, 200/255.0, 200/255.0, 200/255.0);
            cairo_rectangle(cr, x, item_y - MARGIN/2, width, LINE_HEIGHT);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
        
        char display_name[256];
        PangoLayout *layout_to_use;
        if (list->entries[i].is_dir) {
            snprintf(display_name, sizeof(display_name), "%s/", list->entries[i].name);
            layout_to_use = layout_bold;
        } else {
            strncpy(display_name, list->entries[i].name, sizeof(display_name) - 1);
            display_name[sizeof(display_name) - 1] = '\0';
            layout_to_use = layout_normal;
        }

        draw_text(cr, display_name, x + MARGIN, item_y, width - 2 * MARGIN, layout_to_use);
    }
}

void draw_file_list(cairo_t *cr, FileList *list, int x, int y, int width, int height) {
    // Draw path bar at the top
    draw_path_bar(cr, x, y, width, list);
    
    // Draw file entries below
    draw_file_entries(cr, list, x, y + PATH_HEIGHT, width, height - PATH_HEIGHT);
}

void draw_image(cairo_t *cr, int x, int y, int width, int height);
void draw_html_preview(cairo_t *cr, int x, int y, int width, int height);

void draw_info_bar(cairo_t *cr, int x, int y, int width) {
    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, y, width, INFO_HEIGHT);
    cairo_fill(cr);

    // Draw a separator line at bottom of info bar
    cairo_set_source_rgb(cr, 150/255.0, 150/255.0, 150/255.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x, y + INFO_HEIGHT);
    cairo_line_to(cr, x + width, y + INFO_HEIGHT);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);

    if (file_list.count > 0 && file_list.selected >= 0 && file_list.selected < file_list.count) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
        char info[256];
        format_file_info(full_path, file_list.entries[file_list.selected].name, 
                         file_list.entries[file_list.selected].is_dir, info, sizeof(info));
        draw_text(cr, info, x + MARGIN, (INFO_HEIGHT / 2) - 5, width - 2 * MARGIN, layout_small);
    }
}

void draw_text_preview(cairo_t *cr, int x, int y, int width, int height, const char *text) {
    if (!text) return;

    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    pango_layout_set_text(layout_mono, text, -1);
    pango_layout_set_width(layout_mono, (width - 2 * MARGIN) * PANGO_SCALE);
    pango_layout_set_wrap(layout_mono, PANGO_WRAP_WORD);

    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);

    cairo_move_to(cr, x + MARGIN, y + MARGIN);
    pango_cairo_show_layout(cr, layout_mono);
}

void draw_html_preview(cairo_t *cr, int x, int y, int width, int height) {
    draw_text_preview(cr, x, y, width, height, preview_html_text);
}

void draw_pdf_preview(cairo_t *cr, int x, int y, int width, int height) {
    draw_text_preview(cr, x, y, width, height, preview_pdf_text);
}

void draw_text_content_preview(cairo_t *cr, int x, int y, int width, int height) {
    draw_text_preview(cr, x, y, width, height, preview_text_content);
}

void draw_media_preview(cairo_t *cr, int x, int y, int width, int height) {
    draw_text_preview(cr, x, y, width, height, preview_media_text);
}

void draw_preview(cairo_t *cr, int x, int y, int width, int height) {
    // Draw info bar at the top
    draw_info_bar(cr, x, y, width);

    // Draw the rest of the preview
    int preview_y = y + INFO_HEIGHT;
    int preview_height = height - INFO_HEIGHT;

    if (preview_height <= 0) return;

    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, preview_y, width, preview_height);
    cairo_fill(cr);

    if (preview_is_image) {
        draw_image(cr, x, preview_y, width, preview_height);
        return;
    }

    if (preview_is_pdf) {
        draw_pdf_preview(cr, x, preview_y, width, preview_height);
        return;
    }

    if (preview_is_text) {
        draw_text_content_preview(cr, x, preview_y, width, preview_height);
        return;
    }

    if (preview_is_html) {
        draw_html_preview(cr, x, preview_y, width, preview_height);
        return;
    }

    if (preview_is_media) {
        draw_media_preview(cr, x, preview_y, width, preview_height);
        return;
    }

    if (preview_is_dir && preview_list.count > 0) {
        draw_file_entries(cr, &preview_list, x, preview_y, width, preview_height);
        return;
    }

    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
    
    if (file_list.count > 0 && file_list.selected >= 0 && file_list.selected < file_list.count) {
        if (file_list.entries[file_list.selected].is_dir) {
            draw_file_entries(cr, &preview_list, x, preview_y, width, preview_height);
        } else if (preview_is_image) {
            draw_image(cr, x, preview_y, width, preview_height);
        } else if (preview_is_pdf) {
            draw_pdf_preview(cr, x, preview_y, width, preview_height);
        } else if (preview_is_text) {
            draw_text_content_preview(cr, x, preview_y, width, preview_height);
        } else if (preview_is_html) {
            draw_html_preview(cr, x, preview_y, width, preview_height);
        } else if (preview_is_media) {
            draw_media_preview(cr, x, preview_y, width, preview_height);
        } else {
            char preview_text[256];
            snprintf(preview_text, sizeof(preview_text), "File: %s", file_list.entries[file_list.selected].name);
            draw_text(cr, preview_text, x + MARGIN, preview_y + MARGIN + LINE_HEIGHT, width - 2 * MARGIN, layout_normal);
        }
    } else {
        draw_text(cr, "No selection", x + MARGIN, preview_y + MARGIN + LINE_HEIGHT, width - 2 * MARGIN, layout_normal);
    }
}

void draw_image(cairo_t *cr, int x, int y, int width, int height) {
    if (!preview_image) return;

    int img_width = gdk_pixbuf_get_width(preview_image);
    int img_height = gdk_pixbuf_get_height(preview_image);

    // Calculate scaled dimensions maintaining aspect ratio
    double scale = 1.0;
    if (img_width > width) {
        scale = (double)width / img_width;
    }
    if (img_height * scale > height) {
        scale = (double)height / img_height;
    }

    int draw_width = (int)(img_width * scale);
    int draw_height = (int)(img_height * scale);
    int draw_x = x + (width - draw_width) / 2;
    int draw_y = y + (height - draw_height) / 2;

    // Scale the pixbuf to fit
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(preview_image, draw_width, draw_height, GDK_INTERP_BILINEAR);
    
    int s_width = gdk_pixbuf_get_width(scaled);
    int s_height = gdk_pixbuf_get_height(scaled);
    int s_n_channels = gdk_pixbuf_get_n_channels(scaled);
    
    // Ensure we have alpha channel
    if (s_n_channels != 4) {
        GdkPixbuf *with_alpha = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
        g_object_unref(scaled);
        scaled = with_alpha;
        s_n_channels = 4;
    }
    
    // Get pixbuf data
    guchar *pixels = gdk_pixbuf_get_pixels(scaled);
    int rowstride = gdk_pixbuf_get_rowstride(scaled);
    
    // Create cairo surface from pixbuf data
    // GDK pixbuf format: RGB(A) non-premultiplied, 8 bits per channel
    // cairo ARGB32: ARGB premultiplied alpha, native endian
    // We need to convert RGB(A) non-premultiplied to ARGB premultiplied
    guchar *image_data = g_malloc0(s_height * s_width * 4);
    for (int py = 0; py < s_height; py++) {
        guchar *src = pixels + py * rowstride;
        guint32 *dst = (guint32*)(image_data + py * s_width * 4);
        for (int px = 0; px < s_width; px++) {
            double a = src[3] / 255.0;
            // Premultiply RGB by alpha
            guint8 r = (guint8)(src[0] * a);
            guint8 g = (guint8)(src[1] * a);
            guint8 b = (guint8)(src[2] * a);
            guint8 alpha = (guint8)(a * 255);
            // Store as 0xAARRGGBB (little-endian: B,G,R,A)
            // But cairo ARGB32 expects native endian, so on little-endian: B,G,R,A
            dst[px] = (alpha << 24) | (r << 16) | (g << 8) | b;
            src += 4;
        }
    }
    
    cairo_surface_t *image_surface = cairo_image_surface_create_for_data(
        image_data, CAIRO_FORMAT_ARGB32, s_width, s_height, s_width * 4
    );
    cairo_set_source_surface(cr, image_surface, draw_x, draw_y);
    cairo_paint(cr);
    cairo_surface_destroy(image_surface);
    g_free(image_data);
    g_object_unref(scaled);
}

void update_preview() {
    free_preview_image();
    free_preview_html();
    free_preview_pdf();
    free_preview_text();
    free_preview_media();
    preview_is_dir = 0;
    
    // Clear preview_list if the selected item is not a directory
    if (file_list.count > 0 && file_list.selected >= 0 && file_list.selected < file_list.count) {
        if (!file_list.entries[file_list.selected].is_dir) {
            free_file_list(&preview_list);
            init_file_list(&preview_list, ".");
        }
    } else {
        free_file_list(&preview_list);
        init_file_list(&preview_list, ".");
    }

    if (file_list.count > 0 && file_list.selected >= 0 && file_list.selected < file_list.count) {
        if (file_list.entries[file_list.selected].is_dir) {
            char preview_path[4096];
            snprintf(preview_path, sizeof(preview_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            load_directory(&preview_list, preview_path);
            preview_is_dir = 1;
        } else if (is_image_file(file_list.entries[file_list.selected].name)) {
            char image_path[4096];
            snprintf(image_path, sizeof(image_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            // Only preview images smaller than ~1MB (1048576 bytes)
            if (is_small_image(image_path, 1048576)) {
                GError *error = NULL;
                preview_image = gdk_pixbuf_new_from_file(image_path, &error);
                if (error) {
                    g_error_free(error);
                    preview_image = NULL;
                }
                if (preview_image) {
                    preview_is_image = 1;
                }
            }
        } else if (is_text_file(file_list.entries[file_list.selected].name)) {
            char text_path[4096];
            snprintf(text_path, sizeof(text_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            // Only preview text files smaller than ~1MB
            if (is_small_image(text_path, 1048576)) {
                preview_text_content = load_text_content(text_path, 1048576);
                if (preview_text_content && preview_text_content[0] != '\0') {
                    preview_is_text = 1;
                } else {
                    free(preview_text_content);
                    preview_text_content = NULL;
                }
            }
        } else if (is_mp3_file(file_list.entries[file_list.selected].name)) {
            char media_path[4096];
            snprintf(media_path, sizeof(media_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            preview_media_text = load_text_preview("mp3info", "-x", NULL, media_path);
            if (preview_media_text && preview_media_text[0] != '\0') {
                preview_is_media = 1;
            } else {
                free(preview_media_text);
                preview_media_text = NULL;
            }
        } else if (is_pdf_file(file_list.entries[file_list.selected].name)) {
            char pdf_path[4096];
            snprintf(pdf_path, sizeof(pdf_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            // Only preview PDF files smaller than ~5MB
            if (is_small_image(pdf_path, 5 * 1048576)) {
                preview_pdf_text = load_pdf_preview(pdf_path);
                if (preview_pdf_text && preview_pdf_text[0] != '\0') {
                    preview_is_pdf = 1;
                } else {
                    free(preview_pdf_text);
                    preview_pdf_text = NULL;
                }
            }
        } else if (is_html_file(file_list.entries[file_list.selected].name)) {
            char html_path[4096];
            snprintf(html_path, sizeof(html_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            preview_html_text = load_html_preview(html_path);
            if (preview_html_text && preview_html_text[0] != '\0') {
                preview_is_html = 1;
            } else {
                free(preview_html_text);
                preview_html_text = NULL;
            }
        } else if (is_media_file(file_list.entries[file_list.selected].name)) {
            char media_path[4096];
            snprintf(media_path, sizeof(media_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
            preview_media_text = load_media_preview(media_path);
            if (preview_media_text && preview_media_text[0] != '\0') {
                preview_is_media = 1;
            } else {
                free(preview_media_text);
                preview_media_text = NULL;
            }
        }
    }
}

void draw_ui(int win_width, int win_height) {
    int left_width = (int)(win_width * PANE_RATIO);
    int right_width = win_width - left_width;

    cairo_surface_t *surface = cairo_xlib_surface_create(dpy, win, DefaultVisual(dpy, screen), win_width, win_height);
    cairo_t *cr = cairo_create(surface);

    // Initialize Pango objects on first draw
    if (!layout_normal) {
        init_pango_objects(cr);
    }

    draw_file_list(cr, &file_list, 0, 0, left_width, win_height);
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
            if (file_list.count > 0) {
                file_list.selected = (file_list.selected + 1) % file_list.count;
                update_preview();
            }
            break;
        case XK_k:
            if (file_list.count > 0) {
                file_list.selected = (file_list.selected - 1 + file_list.count) % file_list.count;
                update_preview();
            }
            break;
        case XK_l:
            if (file_list.count > 0 && file_list.entries[file_list.selected].is_dir) {
                char new_path[4096];
                snprintf(new_path, sizeof(new_path), "%s/%s", file_list.path, file_list.entries[file_list.selected].name);
                load_directory(&file_list, new_path);
                update_preview();
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
                char *last_slash = strrchr(file_list.path, '/');
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
            update_preview();
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

    init_file_list(&file_list, ".");
    load_directory(&file_list, ".");
    init_file_list(&preview_list, ".");
    preview_is_dir = 0;

    Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    update_preview();

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

    free_file_list(&file_list);
    free_file_list(&preview_list);
    free_preview_image();
    free_preview_html();
    free_preview_pdf();
    free_preview_text();
    free_preview_media();
    free_pango_objects();
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
