#ifndef PREVIEW_H
#define PREVIEW_H

#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include "filelist.h"

typedef struct {
    GdkPixbuf *pixbuf;
    int width;
    int height;
} ScaledImageCache;

typedef enum {
    PREVIEW_RESULT_NONE,
    PREVIEW_RESULT_IMAGE,
    PREVIEW_RESULT_TEXT,
    PREVIEW_RESULT_HTML,
    PREVIEW_RESULT_MEDIA,
    PREVIEW_RESULT_PDF
} PreviewResultKind;

typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    char *path;
} PreviewTask;

typedef struct {
    unsigned long generation;
    PreviewResultKind kind;
    char *text;
    GdkPixbuf *image;
    FileList list;
    int is_dir;
} PreviewResult;

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
extern ScaledImageCache scaled_image_cache;

void free_preview_image(void);
void free_preview_html(void);
void free_preview_pdf(void);
void free_preview_text(void);
void free_preview_media(void);
void free_scaled_image_cache(void);
void clear_preview_state(void);
void free_preview_result(PreviewResult *result);
void apply_preview_result(void);
void draw_preview(cairo_t *cr, int x, int y, int width, int height);

#endif
