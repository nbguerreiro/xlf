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
            dst[px] = ((guint32)alpha << 24) | ((guint32)r << 16) |
                      ((guint32)g << 8) | (guint32)b;
            src += 4;
        }
    }
    
    cairo_surface_mark_dirty(image_surface);
    cairo_set_source_surface(cr, image_surface, draw_x, draw_y);
    cairo_paint(cr);
    cairo_surface_destroy(image_surface);
    g_object_unref(scaled);
}