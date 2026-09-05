#include "preview.h"
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


extern void draw_file_entries(cairo_t *cr, FileList *list, int x, int y, int width, int height);
extern void draw_text(cairo_t *cr, const char *text, int x, int y, int width, PangoLayout *layout);
extern PangoLayout *layout_normal;
extern PangoLayout *layout_mono;
extern int layout_small;
extern void format_file_info(const char *path, const char *name, int is_dir, char *out, size_t out_size);
extern int INFO_HEIGHT;
extern int LINE_HEIGHT;
extern int MARGIN;
extern int BG_R, BG_G, BG_B, TEXT_R, TEXT_G, TEXT_B;
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

