#include "preview.h"
#include <stdlib.h>
#include <glib-object.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

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

