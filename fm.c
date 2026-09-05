#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include "filelist.h"
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


typedef struct {
    GdkPixbuf *pixbuf;
    int width;
    int height;
} ScaledImageCache;

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
typedef enum {
    PREVIEW_RESULT_NONE,
    PREVIEW_RESULT_DIR,
    PREVIEW_RESULT_IMAGE,
    PREVIEW_RESULT_TEXT,
    PREVIEW_RESULT_HTML,
    PREVIEW_RESULT_PDF,
    PREVIEW_RESULT_MEDIA
} PreviewResultKind;

typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    char *path;
} PreviewTask;

typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    GdkPixbuf *image;
    char *text;
    FileList directory;
    int has_directory;
} PreviewResult;

pthread_t preview_thread;
pthread_mutex_t preview_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t preview_cond = PTHREAD_COND_INITIALIZER;
PreviewTask preview_task = {0, PREVIEW_RESULT_NONE, NULL};
int preview_task_pending = 0;
int preview_worker_stop = 0;
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

// Tool availability cache (checked once at startup)
int tool_lynx_available = 0;
int tool_pdfinfo_available = 0;
int tool_mediainfo_available = 0;
int tool_mp3info_available = 0;
char status_message[256] = "";

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

void free_scaled_image_cache() {
    if (scaled_image_cache.pixbuf) {
        g_object_unref(scaled_image_cache.pixbuf);
        scaled_image_cache.pixbuf = NULL;
    }
    scaled_image_cache.width = 0;
    scaled_image_cache.height = 0;
}

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
    if (!layout_path) {
        layout_path = pango_cairo_create_layout(cr);
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
    if (!desc_path) {
        desc_path = pango_font_description_from_string("Sans 11");
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
    if (layout_path) {
        pango_layout_set_font_description(layout_path, desc_path);
        pango_layout_set_ellipsize(layout_path, PANGO_ELLIPSIZE_MIDDLE);
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
    if (layout_path) {
        g_object_unref(layout_path);
        layout_path = NULL;
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
    if (desc_path) {
        pango_font_description_free(desc_path);
        desc_path = NULL;
    }
}

int is_image_file(const char *filename);
int is_pdf_file(const char *filename);
int is_text_file(const char *filename);
int is_html_file(const char *filename);
int is_mp3_file(const char *filename);
int is_media_file(const char *filename);

typedef enum {
    FILE_TYPE_UNKNOWN,
    FILE_TYPE_IMAGE,
    FILE_TYPE_TEXT,
    FILE_TYPE_HTML,
    FILE_TYPE_PDF,
    FILE_TYPE_MP3,
    FILE_TYPE_MEDIA
} FileType;

FileType detect_file_type_mime(const char *path) {
    GFile *file;
    GFileInfo *info;
    const char *content_type;
    FileType type = FILE_TYPE_UNKNOWN;

    if (!path) return FILE_TYPE_UNKNOWN;

    file = g_file_new_for_path(path);
    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE, NULL, NULL);
    g_object_unref(file);
    if (!info) return FILE_TYPE_UNKNOWN;

    content_type = g_file_info_get_content_type(info);
    if (content_type) {
        if (g_str_has_prefix(content_type, "image/")) type = FILE_TYPE_IMAGE;
        else if (g_content_type_is_a(content_type, "text/html") ||
                 g_content_type_is_a(content_type, "application/xhtml+xml")) type = FILE_TYPE_HTML;
        else if (g_content_type_is_a(content_type, "application/pdf")) type = FILE_TYPE_PDF;
        else if (g_content_type_is_a(content_type, "audio/mpeg")) type = FILE_TYPE_MP3;
        else if (g_str_has_prefix(content_type, "text/")) type = FILE_TYPE_TEXT;
        else if (g_str_has_prefix(content_type, "audio/") ||
                 g_str_has_prefix(content_type, "video/")) type = FILE_TYPE_MEDIA;
    }

    g_object_unref(info);
    return type;
}

FileType detect_file_type(const char *path, const char *filename) {
    FileType type = detect_file_type_mime(path);

    if (type == FILE_TYPE_IMAGE && !ENABLE_PREVIEW_IMAGE) return FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_TEXT && !ENABLE_PREVIEW_TEXT) return FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_HTML && !ENABLE_PREVIEW_HTML) return FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_PDF && !ENABLE_PREVIEW_PDF) return FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_MP3 && !ENABLE_PREVIEW_MP3) return FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_MEDIA && !ENABLE_PREVIEW_MEDIA) return FILE_TYPE_UNKNOWN;
    if (type != FILE_TYPE_UNKNOWN) return type;

    if (ENABLE_PREVIEW_IMAGE && is_image_file(filename)) return FILE_TYPE_IMAGE;
    if (ENABLE_PREVIEW_TEXT && is_text_file(filename)) return FILE_TYPE_TEXT;
    if (ENABLE_PREVIEW_HTML && is_html_file(filename)) return FILE_TYPE_HTML;
    if (ENABLE_PREVIEW_PDF && is_pdf_file(filename)) return FILE_TYPE_PDF;
    if (ENABLE_PREVIEW_MP3 && is_mp3_file(filename)) return FILE_TYPE_MP3;
    if (ENABLE_PREVIEW_MEDIA && is_media_file(filename)) return FILE_TYPE_MEDIA;
    return FILE_TYPE_UNKNOWN;
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

static int valid_preview_path(const char *path) {
    if (!path || path[0] == '\0') return 0;

    // exec-family calls do not invoke a shell, but control characters in a
    // pathname can still confuse helper programs or produce unsafe output.
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
        if (*p < 0x20 || *p == 0x7f) return 0;
    }

    return 1;
}

static int valid_preview_command(const char *cmd) {
    static const char *const allowed[] = {
        "lynx", "pdfinfo", "mediainfo", "mp3info"
    };

    if (!cmd || cmd[0] == '\0') return 0;

    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        if (strcmp(cmd, allowed[i]) == 0) return 1;
    }

    return 0;
}

char *load_text_preview(const char *cmd, const char *arg1, const char *arg2, const char *path) {
    if (!valid_preview_command(cmd) || !valid_preview_path(path)) return NULL;

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
    if (!tool_lynx_available) {
        return strdup("Tool 'lynx' not found.\nInstall lynx to preview HTML files.\n\n"
                      "On Ubuntu/Debian: sudo apt install lynx\n"
                      "On macOS: brew install lynx\n"
                      "On Fedora: sudo dnf install lynx");
    }
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

    if (n == 0) {
        free(buffer);
        return NULL;
    }

    buffer[n] = '\0';
    return buffer;
}

char *load_pdf_preview(const char *path) {
    if (!tool_pdfinfo_available) {
        return strdup("Tool 'pdfinfo' not found.\nInstall poppler-utils to preview PDF metadata.\n\n"
                      "On Ubuntu/Debian: sudo apt install poppler-utils\n"
                      "On macOS: brew install poppler\n"
                      "On Fedora: sudo dnf install poppler-utils");
    }
    return load_text_preview("pdfinfo", NULL, NULL, path);
}

char *load_media_preview(const char *path) {
    if (!tool_mediainfo_available) {
        return strdup("Tool 'mediainfo' not found.\nInstall mediainfo to preview audio/video metadata.\n\n"
                      "On Ubuntu/Debian: sudo apt install mediainfo\n"
                      "On macOS: brew install mediainfo\n"
                      "On Fedora: sudo dnf install mediainfo");
    }
    return load_text_preview("mediainfo", NULL, NULL, path);
}

char *load_mp3_info(const char *path) {
    if (!tool_mp3info_available) {
        return strdup("Tool 'mp3info' not found.\nInstall mp3info to preview MP3 metadata.\n\n"
                      "On Ubuntu/Debian: sudo apt install mp3info\n"
                      "On macOS: brew install mp3info\n"
                      "On Fedora: sudo dnf install mp3info");
    }
    return load_text_preview("mp3info", "-x", NULL, path);
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
    const char *home = getenv("HOME");
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
    const struct tm *tm = localtime(&st.st_mtime);
    char date_str[64];
    strftime(date_str, sizeof(date_str), "%a %b %d %H:%M:%S %Y", tm);

    snprintf(buf, buf_size, "%s %ld %ld %s %s", perms, (long)st.st_uid, (long)st.st_gid, size_str, date_str);
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

    PangoLayout *path_layout = layout_path;
    cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
    
    cairo_move_to(cr, x + MARGIN, (PATH_HEIGHT / 2) - 5);
    pango_layout_set_text(path_layout, display_path ? display_path : "", -1);
    pango_cairo_show_layout(cr, path_layout);
    free(display_path);
}

static int search_matches(const FileEntry *entry) {
    return !search_active || search_query_len == 0 ||
           strcasestr(entry->name, search_query) != NULL;
}

static int next_search_match(int start, int direction) {
    if (file_list.count <= 0) return -1;
    int index = start;
    for (int i = 0; i < file_list.count; ++i) {
        index = (index + direction + file_list.count) % file_list.count;
        if (search_matches(&file_list.entries[index])) return index;
    }
    return -1;
}

void draw_file_entries(cairo_t *cr, const FileList *list, int x, int y, int width, int height) {
    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);

    int visible_items = height / LINE_HEIGHT;
    int row = 0;
    for (int i = 0; i < list->count && row < visible_items; i++) {
        if (!search_matches(&list->entries[i])) continue;

        int item_y = y + row * LINE_HEIGHT + MARGIN;

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

        draw_text(cr, display_name, x + MARGIN, item_y,
                  width - 2 * MARGIN, layout_to_use);
        row++;
    }
}

static void set_status(const char *message) {
    if (!message) {
        status_message[0] = '\0';
        return;
    }
    snprintf(status_message, sizeof(status_message), "%s", message);
}

void draw_file_list(cairo_t *cr, FileList *list, int x, int y, int width, int height) {
    // Draw search/rename feedback in the path bar.
    if (rename_active) {
        cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
        cairo_rectangle(cr, x, y, width, PATH_HEIGHT);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
        char rename_display[SEARCH_MAX + 10];
        snprintf(rename_display, sizeof(rename_display), "Rename: %s", rename_query);
        draw_text(cr, rename_display, x + MARGIN, (PATH_HEIGHT / 2) - 5,
                  width - 2 * MARGIN, layout_path);
    } else if (search_active) {
        cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
        cairo_rectangle(cr, x, y, width, PATH_HEIGHT);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
        char search_display[SEARCH_MAX + 4];
        snprintf(search_display, sizeof(search_display), "/%s", search_query);
        draw_text(cr, search_display, x + MARGIN, (PATH_HEIGHT / 2) - 5,
                  width - 2 * MARGIN, layout_path);
    } else {
        draw_path_bar(cr, x, y, width, list);
    }
    
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

    // Check if cached scaled image is valid (dimensions match)
    if (scaled_image_cache.pixbuf && 
        scaled_image_cache.width == draw_width && 
        scaled_image_cache.height == draw_height) {
        // Use cached scaled image
        GdkPixbuf *scaled = scaled_image_cache.pixbuf;
        
        int s_width = gdk_pixbuf_get_width(scaled);
        int s_height = gdk_pixbuf_get_height(scaled);
        int s_n_channels = gdk_pixbuf_get_n_channels(scaled);
        
        // Ensure we have alpha channel
        if (s_n_channels != 4) {
            GdkPixbuf *with_alpha = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
            g_object_unref(scaled);
            scaled = with_alpha;
            }
        
        guchar *pixels = gdk_pixbuf_get_pixels(scaled);
        int rowstride = gdk_pixbuf_get_rowstride(scaled);
        
        // Create cairo surface with owned memory
        cairo_surface_t *image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, s_width, s_height);
        if (cairo_surface_status(image_surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(image_surface);
            return;
        }
        
        guchar *surface_data = cairo_image_surface_get_data(image_surface);
        int surface_stride = cairo_image_surface_get_stride(image_surface);
        
        for (int py = 0; py < s_height; py++) {
            guchar *src = pixels + py * rowstride;
            guint32 *dst = (guint32*)(surface_data + py * surface_stride);
            for (int px = 0; px < s_width; px++) {
                double a = src[3] / 255.0;
                guint8 r = (guint8)(src[0] * a);
                guint8 g = (guint8)(src[1] * a);
                guint8 b = (guint8)(src[2] * a);
                guint8 alpha = (guint8)(a * 255);
                dst[px] = (alpha << 24) | (r << 16) | (g << 8) | b;
                src += 4;
            }
        }
        
        cairo_surface_mark_dirty(image_surface);
        cairo_set_source_surface(cr, image_surface, draw_x, draw_y);
        cairo_paint(cr);
        cairo_surface_destroy(image_surface);
        
        if (s_n_channels != gdk_pixbuf_get_n_channels(scaled_image_cache.pixbuf)) {
            g_object_unref(scaled);
        }
        return;
    }

    // Scale the pixbuf and cache it
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(preview_image, draw_width, draw_height, GDK_INTERP_BILINEAR);
    
    int s_width = gdk_pixbuf_get_width(scaled);
    int s_height = gdk_pixbuf_get_height(scaled);
    int s_n_channels = gdk_pixbuf_get_n_channels(scaled);
    
    // Ensure we have alpha channel
    if (s_n_channels != 4) {
        GdkPixbuf *with_alpha = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
        g_object_unref(scaled);
        scaled = with_alpha;
    }
    
    // Update cache
    free_scaled_image_cache();
    scaled_image_cache.pixbuf = g_object_ref(scaled);
    scaled_image_cache.width = s_width;
    scaled_image_cache.height = s_height;
    
    // Get pixbuf data and render using cairo's owned surface
    guchar *pixels = gdk_pixbuf_get_pixels(scaled);
    int rowstride = gdk_pixbuf_get_rowstride(scaled);
    
    // Create cairo surface with owned memory
    cairo_surface_t *image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, s_width, s_height);
    if (cairo_surface_status(image_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(image_surface);
        g_object_unref(scaled);
        return;
    }
    
    guchar *surface_data = cairo_image_surface_get_data(image_surface);
    int surface_stride = cairo_image_surface_get_stride(image_surface);
    
    for (int py = 0; py < s_height; py++) {
        guchar *src = pixels + py * rowstride;
        guint32 *dst = (guint32*)(surface_data + py * surface_stride);
        for (int px = 0; px < s_width; px++) {
            double a = src[3] / 255.0;
            guint8 r = (guint8)(src[0] * a);
            guint8 g = (guint8)(src[1] * a);
            guint8 b = (guint8)(src[2] * a);
            guint8 alpha = (guint8)(a * 255);
            dst[px] = (alpha << 24) | (r << 16) | (g << 8) | b;
            src += 4;
        }
    }
    
    cairo_surface_mark_dirty(image_surface);
    cairo_set_source_surface(cr, image_surface, draw_x, draw_y);
    cairo_paint(cr);
    cairo_surface_destroy(image_surface);
    g_object_unref(scaled);
}

void clear_preview_state() {
    free_preview_image();
    free_preview_html();
    free_preview_pdf();
    free_preview_text();
    free_preview_media();
    free_scaled_image_cache();

    if (preview_list.entries || preview_list.path) {
        free_file_list(&preview_list);
    }
    init_file_list(&preview_list, ".");
    preview_is_dir = 0;
}

void free_preview_result(PreviewResult *result) {
    if (!result) return;

    if (result->image) {
        g_object_unref(result->image);
        result->image = NULL;
    }
    free(result->text);
    result->text = NULL;

    if (result->has_directory) {
        free_file_list(&result->directory);
        result->has_directory = 0;
    }

    result->generation = 0;
    result->kind = PREVIEW_RESULT_NONE;
}

void apply_preview_result() {
    PreviewResult result;

    pthread_mutex_lock(&preview_mutex);
    if (!preview_result_ready) {
        pthread_mutex_unlock(&preview_mutex);
        return;
    }

    result = preview_result;
    preview_result = (PreviewResult){0, PREVIEW_RESULT_NONE, NULL, NULL,
                                     {NULL, 0, 0, 0, NULL}, 0};
    preview_result_ready = 0;
    pthread_mutex_unlock(&preview_mutex);

    // Snapshot the generation under the same mutex used by request_preview().
    pthread_mutex_lock(&preview_mutex);
    unsigned long current_generation = preview_generation;
    pthread_mutex_unlock(&preview_mutex);

    // A newer selection may have superseded this result while the worker was busy.
    if (result.generation != current_generation) {
        free_preview_result(&result);
        return;
    }

    clear_preview_state();

    if (result.kind != PREVIEW_RESULT_NONE && result.kind != PREVIEW_RESULT_DIR &&
        !result.image && !result.text) {
        set_status("Preview could not be loaded.");
    } else if (result.text && strncmp(result.text, "Tool '", 6) == 0) {
        const char *end = strchr(result.text, '\n');
        char message[256];
        size_t len = end ? (size_t)(end - result.text) : strlen(result.text);
        if (len >= sizeof(message)) len = sizeof(message) - 1;
        memcpy(message, result.text, len);
        message[len] = '\0';
        set_status(message);
    } else {
        set_status("");
    }

    switch (result.kind) {
        case PREVIEW_RESULT_DIR:
            preview_list = result.directory;
            result.directory = (FileList){NULL, 0, 0, 0, NULL};
            result.has_directory = 0;
            preview_is_dir = 1;
            break;
        case PREVIEW_RESULT_IMAGE:
            preview_image = result.image;
            result.image = NULL;
            preview_is_image = (preview_image != NULL);
            break;
        case PREVIEW_RESULT_TEXT:
            preview_text_content = result.text;
            result.text = NULL;
            preview_is_text = (preview_text_content != NULL &&
                               preview_text_content[0] != '\0');
            break;
        case PREVIEW_RESULT_HTML:
            preview_html_text = result.text;
            result.text = NULL;
            preview_is_html = (preview_html_text != NULL &&
                               preview_html_text[0] != '\0');
            break;
        case PREVIEW_RESULT_PDF:
            preview_pdf_text = result.text;
            result.text = NULL;
            preview_is_pdf = (preview_pdf_text != NULL &&
                              preview_pdf_text[0] != '\0');
            break;
        case PREVIEW_RESULT_MEDIA:
            preview_media_text = result.text;
            result.text = NULL;
            preview_is_media = (preview_media_text != NULL &&
                                preview_media_text[0] != '\0');
            break;
        case PREVIEW_RESULT_NONE:
            break;
    }

    free_preview_result(&result);
}

PreviewResult load_preview_result(const PreviewTask *task) {
    PreviewResult result = {
        task->generation, task->kind, NULL, NULL,
        {NULL, 0, 0, 0, NULL}, 0
    };

    if (!task->path) return result;

    switch (task->kind) {
        case PREVIEW_RESULT_DIR:
            init_file_list(&result.directory, task->path);
            load_directory(&result.directory, task->path);
            result.has_directory = 1;
            break;

        case PREVIEW_RESULT_IMAGE:
            if (is_small_image(task->path, 1048576)) {
                GError *error = NULL;
                result.image = gdk_pixbuf_new_from_file(task->path, &error);
                if (error) g_error_free(error);
            }
            break;

        case PREVIEW_RESULT_TEXT:
            if (is_small_image(task->path, 1048576)) {
                result.text = load_text_content(task->path, 1048576);
                if (result.text && result.text[0] == '\0') {
                    free(result.text);
                    result.text = NULL;
                }
            }
            break;

        case PREVIEW_RESULT_HTML:
            result.text = load_html_preview(task->path);
            if (result.text && result.text[0] == '\0') {
                free(result.text);
                result.text = NULL;
            }
            break;

        case PREVIEW_RESULT_PDF:
            if (is_small_image(task->path, 5 * 1048576)) {
                result.text = load_pdf_preview(task->path);
                if (result.text && result.text[0] == '\0') {
                    free(result.text);
                    result.text = NULL;
                }
            }
            break;

        case PREVIEW_RESULT_MEDIA:
            result.text = is_mp3_file(task->path)
                ? load_mp3_info(task->path)
                : load_media_preview(task->path);
            if (result.text && result.text[0] == '\0') {
                free(result.text);
                result.text = NULL;
            }
            break;

        case PREVIEW_RESULT_NONE:
            break;
    }

    return result;
}

void *preview_worker_main(void *unused) {
    (void)unused;

    while (1) {
        PreviewTask task;

        pthread_mutex_lock(&preview_mutex);
        while (!preview_task_pending && !preview_worker_stop) {
            pthread_cond_wait(&preview_cond, &preview_mutex);
        }

        if (preview_worker_stop) {
            pthread_mutex_unlock(&preview_mutex);
            break;
        }

        // Transfer ownership of the path to the worker before releasing the lock.
        // The main thread may replace preview_task while this work is in progress.
        task = preview_task;
        preview_task.path = NULL;
        preview_task_pending = 0;
        pthread_mutex_unlock(&preview_mutex);

        PreviewResult result = load_preview_result(&task);
        free(task.path);

        pthread_mutex_lock(&preview_mutex);

        // Discard an older result if a newer preview request has already arrived.
        if (result.generation != preview_generation) {
            pthread_mutex_unlock(&preview_mutex);
            free_preview_result(&result);
            continue;
        }

        if (preview_result_ready) {
            PreviewResult old_result = preview_result;
            preview_result = (PreviewResult){0, PREVIEW_RESULT_NONE, NULL, NULL,
                                             {NULL, 0, 0, 0, NULL}, 0};
            preview_result_ready = 0;
            pthread_mutex_unlock(&preview_mutex);
            free_preview_result(&old_result);
            pthread_mutex_lock(&preview_mutex);
        }

        preview_result = result;
        preview_result_ready = 1;
        pthread_mutex_unlock(&preview_mutex);

        // Wake the main thread without touching Xlib from the worker.
        if (preview_wake_pipe[1] >= 0) {
            const char byte = 'p';
            ssize_t written;
            do {
                written = write(preview_wake_pipe[1], &byte, 1);
            } while (written < 0 && errno == EINTR);
        }
    }

    return NULL;
}

int start_preview_worker() {
    if (pipe(preview_wake_pipe) != 0) {
        preview_wake_pipe[0] = -1;
        preview_wake_pipe[1] = -1;
        return 0;
    }

    int flags = fcntl(preview_wake_pipe[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(preview_wake_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    if (pthread_create(&preview_thread, NULL, preview_worker_main, NULL) != 0) {
        close(preview_wake_pipe[0]);
        close(preview_wake_pipe[1]);
        preview_wake_pipe[0] = -1;
        preview_wake_pipe[1] = -1;
        return 0;
    }

    preview_worker_started = 1;
    return 1;
}

void request_preview() {
    PreviewTask task = {0, PREVIEW_RESULT_NONE, NULL};

    pthread_mutex_lock(&preview_mutex);
    preview_generation++;
    task.generation = preview_generation;

    if (file_list.count > 0 &&
        file_list.selected >= 0 &&
        file_list.selected < file_list.count) {
        const FileEntry *entry = &file_list.entries[file_list.selected];
        char path[4096];

        // ".." is a navigation entry, not a previewable directory. Keep the
        // preview pane blank while it is selected.
        if (strcmp(entry->name, "..") == 0) {
            preview_task = task;
            preview_task_pending = 0;
            pthread_cond_signal(&preview_cond);
            pthread_mutex_unlock(&preview_mutex);
            clear_preview_state();
            return;
        }

        snprintf(path, sizeof(path), "%s/%s", file_list.path, entry->name);
        task.path = strdup(path);

        if (task.path) {
            if (entry->is_dir) {
                task.kind = PREVIEW_RESULT_DIR;
            } else {
                switch (detect_file_type(path, entry->name)) {
                    case FILE_TYPE_IMAGE: task.kind = PREVIEW_RESULT_IMAGE; break;
                    case FILE_TYPE_TEXT: task.kind = PREVIEW_RESULT_TEXT; break;
                    case FILE_TYPE_HTML: task.kind = PREVIEW_RESULT_HTML; break;
                    case FILE_TYPE_PDF: task.kind = PREVIEW_RESULT_PDF; break;
                    case FILE_TYPE_MP3:
                    case FILE_TYPE_MEDIA: task.kind = PREVIEW_RESULT_MEDIA; break;
                    case FILE_TYPE_UNKNOWN: task.kind = PREVIEW_RESULT_NONE; break;
                }
                if (task.kind == PREVIEW_RESULT_NONE) {
                    free(task.path);
                    task.path = NULL;
                }
            }
        }
    }

    free(preview_task.path);
    preview_task = task;
    preview_task_pending = (task.path != NULL);
    pthread_cond_signal(&preview_cond);
    pthread_mutex_unlock(&preview_mutex);

    // Immediately clear stale preview content; the worker will fill it in later.
    clear_preview_state();
}

void stop_preview_worker() {
    pthread_mutex_lock(&preview_mutex);
    preview_worker_stop = 1;
    pthread_cond_signal(&preview_cond);
    pthread_mutex_unlock(&preview_mutex);

    if (preview_worker_started) {
        pthread_join(preview_thread, NULL);
        preview_worker_started = 0;
    }

    pthread_mutex_lock(&preview_mutex);
    free(preview_task.path);
    preview_task.path = NULL;
    preview_task_pending = 0;

    PreviewResult result = preview_result;
    preview_result = (PreviewResult){0, PREVIEW_RESULT_NONE, NULL, NULL,
                                     {NULL, 0, 0, 0, NULL}, 0};
    preview_result_ready = 0;
    pthread_mutex_unlock(&preview_mutex);

    free_preview_result(&result);

    if (preview_wake_pipe[0] >= 0) close(preview_wake_pipe[0]);
    if (preview_wake_pipe[1] >= 0) close(preview_wake_pipe[1]);
    preview_wake_pipe[0] = -1;
    preview_wake_pipe[1] = -1;
}

void free_draw_surfaces() {
    if (backbuffer_surface) {
        cairo_surface_destroy(backbuffer_surface);
        backbuffer_surface = NULL;
    }
    if (window_surface) {
        cairo_surface_destroy(window_surface);
        window_surface = NULL;
    }
    surface_width = 0;
    surface_height = 0;
}

int ensure_draw_surfaces(int width, int height) {
    if (width <= 0 || height <= 0) return 0;

    if (!window_surface) {
        window_surface = cairo_xlib_surface_create(
            dpy, win, DefaultVisual(dpy, screen), width, height);
        if (cairo_surface_status(window_surface) != CAIRO_STATUS_SUCCESS) {
            free_draw_surfaces();
            return 0;
        }
    } else {
        cairo_xlib_surface_set_size(window_surface, width, height);
    }

    if (!backbuffer_surface ||
        surface_width != width || surface_height != height) {
        cairo_surface_t *new_backbuffer =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(new_backbuffer) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(new_backbuffer);
            return 0;
        }

        if (backbuffer_surface) {
            cairo_surface_destroy(backbuffer_surface);
        }
        backbuffer_surface = new_backbuffer;
        surface_width = width;
        surface_height = height;
    }

    return 1;
}

void draw_ui(int win_width, int win_height) {
    int left_width = (int)(win_width * PANE_RATIO);
    int right_width = win_width - left_width;

    if (!ensure_draw_surfaces(win_width, win_height)) return;

    cairo_t *cr = cairo_create(backbuffer_surface);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);
        return;
    }

    // Initialize Pango objects once, using the reusable backbuffer context.
    if (!layout_normal) {
        init_pango_objects(cr);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_file_list(cr, &file_list, 0, 0, left_width, win_height);
    draw_preview(cr, left_width, 0, right_width, win_height);

    if (status_message[0] != '\0') {
        cairo_set_source_rgb(cr, BG_R/255.0, BG_G/255.0, BG_B/255.0);
        cairo_rectangle(cr, 0, win_height - INFO_HEIGHT, win_width, INFO_HEIGHT);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, TEXT_R/255.0, TEXT_G/255.0, TEXT_B/255.0);
        draw_text(cr, status_message, MARGIN, win_height - INFO_HEIGHT + 5,
                  win_width - 2 * MARGIN, layout_small);
    }

    cairo_set_source_rgb(cr, 150/255.0, 150/255.0, 150/255.0);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, left_width, 0);
    cairo_line_to(cr, left_width, win_height);
    cairo_stroke(cr);

    cairo_destroy(cr);

    // Present the completed frame to the X11 window in one operation.
    cairo_t *window_cr = cairo_create(window_surface);
    if (cairo_status(window_cr) == CAIRO_STATUS_SUCCESS) {
        cairo_set_source_surface(window_cr, backbuffer_surface, 0, 0);
        cairo_set_operator(window_cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(window_cr);
        cairo_surface_flush(window_surface);
        cairo_destroy(window_cr);
        XFlush(dpy);
    } else {
        cairo_destroy(window_cr);
    }
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
