#define _GNU_SOURCE
#include "ui.h"
#include "util.h"
#include "preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <cairo-xlib.h>
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
#define SEARCH_MAX 256

extern Display *dpy;
extern Window win;
extern int screen;
extern FileList file_list;
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
extern int search_active;
extern char search_query[SEARCH_MAX];
extern size_t search_query_len;
extern int rename_active;
extern char rename_query[SEARCH_MAX];
extern size_t rename_query_len;
extern char status_message[256];

cairo_surface_t *window_surface = NULL;
cairo_surface_t *backbuffer_surface = NULL;
int surface_width = 0;
int surface_height = 0;

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

int ui_next_search_match(int start, int direction) {
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

void set_status(const char *message) {
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