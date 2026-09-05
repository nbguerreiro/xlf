#include "preview.h"

#include <stdlib.h>
#include <glib-object.h>

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

void free_preview_image(void) {
    if (preview_image) {
        g_object_unref(preview_image);
        preview_image = NULL;
    }
    preview_is_image = 0;
}

void free_preview_html(void) {
    free(preview_html_text);
    preview_html_text = NULL;
    preview_is_html = 0;
}

void free_preview_pdf(void) {
    free(preview_pdf_text);
    preview_pdf_text = NULL;
    preview_is_pdf = 0;
}

void free_preview_text(void) {
    free(preview_text_content);
    preview_text_content = NULL;
    preview_is_text = 0;
}

void free_preview_media(void) {
    free(preview_media_text);
    preview_media_text = NULL;
    preview_is_media = 0;
}

void free_scaled_image_cache(void) {
    if (scaled_image_cache.pixbuf) {
        g_object_unref(scaled_image_cache.pixbuf);
        scaled_image_cache.pixbuf = NULL;
    }
    scaled_image_cache.width = 0;
    scaled_image_cache.height = 0;
}
