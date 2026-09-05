#ifndef UI_H
#define UI_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include "filelist.h"

void init_pango_objects(cairo_t *cr);
void free_pango_objects(void);
void draw_text(cairo_t *cr, const char *text, int x, int y, int width, PangoLayout *layout);
void draw_path_bar(cairo_t *cr, int x, int y, int width, FileList *list);
void draw_file_entries(cairo_t *cr, const FileList *list, int x, int y, int width, int height);
void draw_file_list(cairo_t *cr, FileList *list, int x, int y, int width, int height);
void draw_info_bar(cairo_t *cr, int x, int y, int width);
void draw_text_preview(cairo_t *cr, int x, int y, int width, int height, const char *text);
void draw_html_preview(cairo_t *cr, int x, int y, int width, int height);
void draw_pdf_preview(cairo_t *cr, int x, int y, int width, int height);
void draw_text_content_preview(cairo_t *cr, int x, int y, int width, int height);
void draw_media_preview(cairo_t *cr, int x, int y, int width, int height);
void free_draw_surfaces(void);
int ensure_draw_surfaces(int width, int height);
void draw_ui(int win_width, int win_height);
void set_status(const char *message);

#endif
