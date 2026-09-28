#include "info_modal.h"

static void on_cpu_graph_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    ContainerInfo *info = user_data;
    
    // Background
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_paint(cr);
    
    if (info->history_count == 0) {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_move_to(cr, 10, height / 2.0);
        cairo_show_text(cr, "No data yet...");
        return;
    }
    
    // Draw grid
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 0.5);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 4; i++) {
        double y = height - (i * height / 5.0);
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
        cairo_stroke(cr);
    }
    
    // Determine max CPU for scaling
    double max_cpu = 1.0;
    for (int i = 0; i < info->history_count; i++) {
        if (info->cpu_history[i] > max_cpu) max_cpu = info->cpu_history[i];
    }
    if (max_cpu < 10.0) max_cpu = 10.0; // scale minimum to 10%
    else max_cpu *= 1.2; // 20% headroom
    
    // Draw line graph
    cairo_set_source_rgb(cr, 0.2, 0.8, 0.4); // Green line
    cairo_set_line_width(cr, 2.0);
    
    double step_x = (double)width / 60.0;
    int start_idx = (info->history_idx - info->history_count + 60) % 60;
    
    for (int i = 0; i < info->history_count; i++) {
        int idx = (start_idx + i) % 60;
        double val = info->cpu_history[idx];
        double x = i * step_x;
        double y = height - (val / max_cpu * height);
        
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

static gboolean update_modal_timer(gpointer user_data) {
    GtkWidget *area = user_data;
    gtk_widget_queue_draw(area);
    return G_SOURCE_CONTINUE;
}

void show_container_modal(ContainerInfo *info, GtkWidget *parent) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Container Details");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
        gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    }
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 15);
    gtk_widget_set_margin_end(box, 15);
    gtk_widget_set_margin_top(box, 15);
    gtk_widget_set_margin_bottom(box, 15);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    
    // Header
    gchar *markup = g_strdup_printf("<span size='large' weight='bold'>%s</span>\n<span size='small' foreground='gray'>%s</span>", info->name, info->id);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_box_append(GTK_BOX(box), title);
    
    // Info grid
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
    gtk_box_append(GTK_BOX(box), grid);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Image:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(info->image), 1, 0, 1, 1);
    
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Status:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(info->status), 1, 1, 1, 1);
    
    // Graph Area
    GtkWidget *graph_label = gtk_label_new("CPU Usage History (Last 5 mins)");
    gtk_widget_set_halign(graph_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(graph_label, 15);
    gtk_box_append(GTK_BOX(box), graph_label);
    
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, -1, 120);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_cpu_graph_draw, info, NULL);
    gtk_box_append(GTK_BOX(box), area);
    
    // Timer to redraw graph while modal is open
    guint timer_id = g_timeout_add(1000, update_modal_timer, area);
    g_object_set_data(G_OBJECT(dialog), "timer_id", GUINT_TO_POINTER(timer_id));
    
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_source_remove), GUINT_TO_POINTER(timer_id));
    
    // Logs Area
    GtkWidget *logs_label = gtk_label_new("Logs (Tail 50)");
    gtk_widget_set_halign(logs_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(logs_label, 15);
    gtk_box_append(GTK_BOX(box), logs_label);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 150);
    
    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD_CHAR);
    
    gchar *logs = docker_client_get_logs(info->id);
    if (logs) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, logs, -1);
        g_free(logs);
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, "No logs available or container stopped.", -1);
    }
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void show_network_modal(const gchar *net_name, GtkWidget *parent) {
    GtkWidget *dialog = gtk_window_new();
    gchar *title_str = g_strdup_printf("Network Details: %s", net_name);
    gtk_window_set_title(GTK_WINDOW(dialog), title_str);
    g_free(title_str);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
        gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    }
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 15);
    gtk_widget_set_margin_end(box, 15);
    gtk_widget_set_margin_top(box, 15);
    gtk_widget_set_margin_bottom(box, 15);
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    
    GtkWidget *title = gtk_label_new(NULL);
    gchar *markup = g_strdup_printf("<span size='large' weight='bold'>Network: %s</span>", net_name);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    g_free(markup);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), title);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    
    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);
    
    gchar *net_info = docker_client_inspect_network(net_name);
    if (net_info) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, net_info, -1);
        g_free(net_info);
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
        gtk_text_buffer_set_text(buffer, "Failed to load network configuration.", -1);
    }
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);
    
    gtk_window_present(GTK_WINDOW(dialog));
}
