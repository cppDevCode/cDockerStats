#include "cDockerStats-gtk4/gauge_view.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double percentage; // 0.0 to 1.0
    char *text;
    char *min_text;
    char *max_text;
    GtkWidget *label_val;
    GtkWidget *drawing_area;
} GaugeData;

static void gauge_data_free(gpointer data) {
    GaugeData *gd = data;
    g_free(gd->text);
    g_free(gd->min_text);
    g_free(gd->max_text);
    g_free(gd);
}

static void draw_gauge_arc(cairo_t *cr, double cx, double cy, double radius, double start_angle, double end_angle, double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_arc(cr, cx, cy, radius, start_angle, end_angle);
    cairo_set_line_width(cr, radius * 0.3);
    cairo_stroke(cr);
}

static void on_draw_gauge(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    GaugeData *gd = user_data;
    
    double cx = width / 2.0;
    double cy = height - 20; // Bottom center, leave room for labels
    double radius = MIN(width / 2.0, height) - 25;
    if (radius < 10) radius = 10;
    
    // Draw 4 segments: Green, Yellow, Orange, Red (Left to Right)
    // In cairo, angles are clockwise. 180 degrees is M_PI (left), 0 is 0 (right).
    // Let's use 5 segments to look more like the picture (but with Green on left, Red on right).
    // Left -> Right = M_PI to 0.
    
    double segments = 5.0;
    double segment_angle = M_PI / segments;
    double gap = 0.05; // gap between segments

    // Segment colors
    double colors[5][3] = {
        {0.4, 0.8, 0.2}, // Green
        {0.7, 0.9, 0.2}, // Light green/yellow
        {1.0, 0.8, 0.1}, // Yellow
        {1.0, 0.5, 0.1}, // Orange
        {1.0, 0.2, 0.2}  // Red
    };

    for (int i = 0; i < 5; i++) {
        double start = M_PI + i * segment_angle;
        double end = start + segment_angle - gap;
        draw_gauge_arc(cr, cx, cy, radius, start, end, colors[i][0], colors[i][1], colors[i][2]);
    }
    
    // Draw needle
    // Percentage 0.0 -> M_PI, 1.0 -> 2*M_PI.
    double needle_angle = M_PI + (gd->percentage * M_PI);
    if (needle_angle < M_PI) needle_angle = M_PI;
    if (needle_angle > 2 * M_PI) needle_angle = 2 * M_PI;
    
    double nx = cx + cos(needle_angle) * (radius - 5);
    double ny = cy + sin(needle_angle) * (radius - 5);
    
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2); // Dark grey needle
    cairo_set_line_width(cr, 4);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, nx, ny);
    cairo_stroke(cr);
    
    // Draw needle base
    cairo_arc(cr, cx, cy, 8, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_set_line_width(cr, 3);
    cairo_stroke(cr);

    // Draw min and max text
    cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    
    if (gd->min_text) {
        cairo_move_to(cr, cx - radius, cy + 15);
        cairo_show_text(cr, gd->min_text);
    }
    
    if (gd->max_text) {
        cairo_text_extents_t extents;
        cairo_text_extents(cr, gd->max_text, &extents);
        cairo_move_to(cr, cx + radius - extents.width, cy + 15);
        cairo_show_text(cr, gd->max_text);
    }
}

GtkWidget *gauge_view_new(const char *title) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_hexpand(box, TRUE);
    
    GtkWidget *lbl_title = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_title), "title");
    gtk_box_append(GTK_BOX(box), lbl_title);
    
    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 150, 100);
    
    GaugeData *gd = g_new0(GaugeData, 1);
    gd->percentage = 0.0;
    gd->text = g_strdup("0%");
    gd->drawing_area = da;
    
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), on_draw_gauge, gd, gauge_data_free);
    gtk_box_append(GTK_BOX(box), da);
    
    GtkWidget *lbl_val = gtk_label_new(gd->text);
    // Use monospace font or bold
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_val), "gauge-value");
    gtk_box_append(GTK_BOX(box), lbl_val);
    
    gd->label_val = lbl_val;
    
    // Store GaugeData pointer in the box using g_object_set_data
    g_object_set_data(G_OBJECT(box), "gauge-data", gd);
    
    return box;
}

void gauge_view_set_value(GtkWidget *widget, double percentage, const char *text, const char *min_text, const char *max_text) {
    GaugeData *gd = g_object_get_data(G_OBJECT(widget), "gauge-data");
    if (!gd) return;
    
    if (percentage < 0.0) percentage = 0.0;
    if (percentage > 1.0) percentage = 1.0;
    
    gd->percentage = percentage;
    g_free(gd->text);
    gd->text = g_strdup(text ? text : "");
    
    g_free(gd->min_text);
    gd->min_text = g_strdup(min_text ? min_text : "");
    
    g_free(gd->max_text);
    gd->max_text = g_strdup(max_text ? max_text : "");
    
    gtk_label_set_text(GTK_LABEL(gd->label_val), gd->text);
    gtk_widget_queue_draw(gd->drawing_area);
}
