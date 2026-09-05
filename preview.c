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

