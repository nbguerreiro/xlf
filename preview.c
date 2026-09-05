#include "preview.h"
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <glib-object.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
extern FileList file_list;
extern pthread_t preview_thread;
extern pthread_mutex_t preview_mutex;
extern pthread_cond_t preview_cond;
extern PreviewTask preview_task;
extern int preview_task_pending;
extern int preview_worker_stop;
extern PreviewResult preview_result;
extern int preview_result_ready;
extern int preview_wake_pipe[2];
extern unsigned long preview_generation;
extern int preview_worker_started;

extern int is_small_image(const char *path, off_t max_size);
extern char *load_text_content(const char *path, size_t max_size);
extern char *load_html_preview(const char *path);
extern char *load_pdf_preview(const char *path);
extern char *load_mp3_info(const char *path);
extern char *load_media_preview(const char *path);
extern void load_directory(FileList *list, const char *path);
extern void init_file_list(FileList *list, const char *path);
extern void clear_preview_state(void);
extern int ENABLE_PREVIEW_IMAGE;
extern int ENABLE_PREVIEW_TEXT;
extern int ENABLE_PREVIEW_HTML;
extern int ENABLE_PREVIEW_PDF;
extern int ENABLE_PREVIEW_MP3;
extern int ENABLE_PREVIEW_MEDIA;
extern int tool_lynx_available;
extern int tool_pdfinfo_available;
extern int tool_mediainfo_available;
extern int tool_mp3info_available;




extern pthread_mutex_t preview_mutex;
extern PreviewResult preview_result;
extern int preview_result_ready;
extern unsigned long preview_generation;
extern FileList preview_list;
extern int preview_is_dir;
extern GdkPixbuf *preview_image;
extern int preview_is_image;
extern char *preview_html_text;
extern int preview_is_html;
extern char *preview_pdf_text;
extern int preview_is_pdf;
extern char *preview_media_text;
extern int preview_is_media;
extern char *preview_text_content;
extern int preview_is_text;
extern void set_status(const char *message);
extern void clear_preview_state(void);


extern GdkPixbuf *preview_image;
extern int preview_is_image;
extern char *preview_html_text;
extern int preview_is_html;
extern char *preview_pdf_text;
extern int preview_is_pdf;
extern char *preview_text_content;
extern int preview_is_text;
extern char *preview_media_text;
extern int preview_is_media;
extern ScaledImageCache scaled_image_cache;

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
            result.text = load_mp3_info(task->path)
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
        task.path = g_strdup(path);

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


extern void draw_info_bar(cairo_t *cr, int x, int y, int width);
extern void draw_image(cairo_t *cr, int x, int y, int width, int height);
extern void draw_file_entries(cairo_t *cr, FileList *list, int x, int y, int width, int height);
extern void draw_text(cairo_t *cr, const char *text, int x, int y, int width, PangoLayout *layout);
extern PangoLayout *layout_normal;
extern PangoLayout *layout_mono;
extern int layout_small;
extern void format_file_info(const char *path, const char *name, int is_dir, char *out, size_t out_size);
#define MARGIN 10
#define LINE_HEIGHT 24
#define INFO_HEIGHT 28
#define BG_R 223
#define BG_G 191
#define BG_B 191
#define TEXT_R 0
#define TEXT_G 0
#define TEXT_B 0
extern void draw_text_preview(cairo_t *cr, int x, int y, int width, int height, const char *text);
extern void draw_pdf_preview(cairo_t *cr, int x, int y, int width, int height);
extern void draw_text_content_preview(cairo_t *cr, int x, int y, int width, int height);
extern void draw_html_preview(cairo_t *cr, int x, int y, int width, int height);
extern void draw_media_preview(cairo_t *cr, int x, int y, int width, int height);

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


int is_image_file(const char *filename);
int is_pdf_file(const char *filename);
int is_text_file(const char *filename);
int is_html_file(const char *filename);
int is_mp3_file(const char *filename);
int is_media_file(const char *filename);


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

