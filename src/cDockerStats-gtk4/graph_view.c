#include "cDockerStats-gtk4/graph_view.h"
#include "core/docker_client.h"
#include "cDockerStats-gtk4/container_object.h"
#include "cDockerStats-gtk4/info_modal.h"
#include <graphviz/gvc.h>
#include <math.h>

typedef struct {
    GListModel *store;
    Agraph_t *graph;
    GVC_t *gvc;
    double scale;
    double offset_x;
    double offset_y;
    double drag_start_offset_x;
    double drag_start_offset_y;
    gboolean initialized_layout;
    int last_width;
    int last_height;
    double user_zoom;
} GraphState;

static void update_graph_layout(GraphState *state) {
    if (!state->store) return;
    guint n_items = g_list_model_get_n_items(state->store);
    if (n_items == 0) return;

    if (!state->gvc) state->gvc = gvContext();

    if (state->graph) {
        gvFreeLayout(state->gvc, state->graph);
        agclose(state->graph);
    }
    
    state->graph = agopen("g", Agundirected, 0);
    agattr(state->graph, AGRAPH, "overlap", "false");
    agattr(state->graph, AGRAPH, "splines", "true");
    agattr(state->graph, AGRAPH, "sep", "+20");

    Agnode_t *host_node = agnode(state->graph, "Docker", 1);
    
    for (guint i = 0; i < n_items; i++) {
        ContainerObject *cobj = g_list_model_get_item(state->store, i);
        if (cobj) {
            Agnode_t *n = agnode(state->graph, (char *)cobj->info->name, 1);
            agedge(state->graph, host_node, n, NULL, 1);
            
            for (GList *l = cobj->info->networks; l != NULL; l = l->next) {
                gchar *net_name = g_strdup_printf("net:%s", (gchar *)l->data);
                Agnode_t *net_n = agnode(state->graph, net_name, 1);
                agedge(state->graph, n, net_n, NULL, 1);
                g_free(net_name);
            }
            
            for (GList *l = cobj->info->volumes; l != NULL; l = l->next) {
                gchar *vol_name = g_strdup_printf("vol:%s", (gchar *)l->data);
                Agnode_t *vol_n = agnode(state->graph, vol_name, 1);
                agedge(state->graph, n, vol_n, NULL, 1);
                g_free(vol_name);
            }

            g_object_unref(cobj);
        }
    }

    gvLayout(state->gvc, state->graph, "neato");
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    GraphState *state = user_data;
    
    // Fill background
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_paint(cr);

    if (!state->graph) return;

    double min_x = 99999, max_x = -99999, min_y = 99999, max_y = -99999;
    for (Agnode_t *n = agfstnode(state->graph); n; n = agnxtnode(state->graph, n)) {
        double nx = ND_coord(n).x;
        double ny = ND_coord(n).y;
        if (nx < min_x) min_x = nx;
        if (nx > max_x) max_x = nx;
        if (ny < min_y) min_y = ny;
        if (ny > max_y) max_y = ny;
    }

    double graph_w = max_x - min_x + 100;
    double graph_h = max_y - min_y + 100;
    
    // Always compute the base scale that fits the window
    double base_scale = MIN(width / graph_w, height / graph_h);
    if (base_scale > 2.0) base_scale = 2.0;
    if (base_scale < 0.1) base_scale = 0.1;
    
    double new_scale = base_scale * state->user_zoom;

    if (!state->initialized_layout) {
        state->scale = new_scale;
        state->offset_x = (width - (max_x + min_x) * state->scale) / 2.0;
        state->offset_y = (height - (max_y + min_y) * state->scale) / 2.0;
        state->initialized_layout = TRUE;
    } else {
        if (state->last_width != 0 && state->last_height != 0 &&
           (state->last_width != width || state->last_height != height)) {
            
            // Adjust offset to keep center point fixed
            double center_x = state->last_width / 2.0;
            double center_y = state->last_height / 2.0;
            
            // Adjust offset for scale change
            state->offset_x = center_x - (center_x - state->offset_x) * (new_scale / state->scale);
            state->offset_y = center_y - (center_y - state->offset_y) * (new_scale / state->scale);
            
            // Adjust offset for resize change
            state->offset_x += (width - state->last_width) / 2.0;
            state->offset_y += (height - state->last_height) / 2.0;
            
            state->scale = new_scale;
        } else if (new_scale != state->scale) {
            // e.g. nodes changed causing bounding box to change, keep same user_zoom
            double center_x = width / 2.0;
            double center_y = height / 2.0;
            state->offset_x = center_x - (center_x - state->offset_x) * (new_scale / state->scale);
            state->offset_y = center_y - (center_y - state->offset_y) * (new_scale / state->scale);
            state->scale = new_scale;
        }
    }
    
    state->last_width = width;
    state->last_height = height;

    // Draw edges
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.6);
    cairo_set_line_width(cr, 2.0);
    for (Agnode_t *n = agfstnode(state->graph); n; n = agnxtnode(state->graph, n)) {
        for (Agedge_t *e = agfstout(state->graph, n); e; e = agnxtout(state->graph, e)) {
            Agnode_t *head = aghead(e);
            double nx1 = ND_coord(n).x * state->scale + state->offset_x;
            double ny1 = ND_coord(n).y * state->scale + state->offset_y;
            double nx2 = ND_coord(head).x * state->scale + state->offset_x;
            double ny2 = ND_coord(head).y * state->scale + state->offset_y;
            cairo_move_to(cr, nx1, ny1);
            cairo_line_to(cr, nx2, ny2);
            cairo_stroke(cr);
        }
    }

    // Draw nodes
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    for (Agnode_t *n = agfstnode(state->graph); n; n = agnxtnode(state->graph, n)) {
        double nx = ND_coord(n).x * state->scale + state->offset_x;
        double ny = ND_coord(n).y * state->scale + state->offset_y;
        double radius = 30 * state->scale;
        if (radius < 15) radius = 15;
        if (radius > 40) radius = 40;

        cairo_new_path(cr); // PREVENT PATHS FROM CONNECTING BETWEEN NODES
        
        gchar *label = agnameof(n);
        if (g_strcmp0(label, "Docker") == 0) {
            cairo_arc(cr, nx, ny, radius, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.14, 0.59, 0.81); // Docker blue
        } else if (g_str_has_prefix(label, "net:")) {
            radius *= 0.6;
            cairo_arc(cr, nx, ny, radius, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.55, 0.35, 0.75); // Purple network
            label += 4; // skip "net:" prefix
        } else if (g_str_has_prefix(label, "vol:")) {
            radius *= 0.5;
            cairo_arc(cr, nx, ny, radius, 0, 2 * M_PI);
            cairo_set_source_rgb(cr, 0.85, 0.65, 0.20); // Yellow/Orange volume
            label += 4; // skip "vol:" prefix
        } else {
            cairo_arc(cr, nx, ny, radius, 0, 2 * M_PI);
            
            double alpha = 1.0;
            gboolean blinking = FALSE;
            
            // Search store for this container to get state
            guint n_items = g_list_model_get_n_items(state->store);
            for (guint i = 0; i < n_items; i++) {
                ContainerObject *cobj = g_list_model_get_item(state->store, i);
                if (cobj && g_strcmp0(cobj->info->name, label) == 0) {
                    if (g_strcmp0(cobj->info->state, "exited") == 0 || g_strcmp0(cobj->info->state, "created") == 0) {
                        alpha = 0.5;
                    } else if (g_strcmp0(cobj->info->state, "restarting") == 0 || g_strcmp0(cobj->info->state, "starting") == 0) {
                        blinking = TRUE;
                    }
                    g_object_unref(cobj);
                    break;
                }
                if (cobj) g_object_unref(cobj);
            }
            
            if (blinking) {
                gint64 ms = g_get_monotonic_time() / 1000;
                alpha = (ms % 1000 < 500) ? 1.0 : 0.3;
            }
            
            cairo_set_source_rgba(cr, 0.18, 0.8, 0.44, alpha); // Green for containers
        }
        
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke(cr);

        // Draw label
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, label, &ext);
        cairo_move_to(cr, nx - ext.width/2, ny + ext.height/2);
        cairo_show_text(cr, label);
    }
}

static void on_store_items_changed(GListModel *model, guint position, guint removed, guint added, gpointer user_data) {
    GtkWidget *area = user_data;
    GraphState *state = (GraphState *)g_object_get_data(G_OBJECT(area), "state");
    if (state) update_graph_layout(state);
    gtk_widget_queue_draw(area);
}

static gboolean periodic_update_cb(gpointer user_data) {
    GtkWidget *area = user_data;
    GraphState *state = (GraphState *)g_object_get_data(G_OBJECT(area), "state");
    if (state) update_graph_layout(state);
    gtk_widget_queue_draw(area);
    return G_SOURCE_CONTINUE;
}

static gboolean blink_timer_cb(gpointer user_data) {
    GtkWidget *area = user_data;
    gtk_widget_queue_draw(area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_graph_scroll(GtkEventControllerScroll *scroll, double dx, double dy, gpointer user_data) {
    GraphState *state = user_data;
    if (dy == 0) return GDK_EVENT_PROPAGATE;
    
    double zoom_factor = (dy < 0) ? 1.1 : 0.9;
    
    if (state->user_zoom * zoom_factor < 0.05) return GDK_EVENT_STOP;
    if (state->user_zoom * zoom_factor > 10.0) return GDK_EVENT_STOP;
    
    state->user_zoom *= zoom_factor;
    
    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(scroll));
    gtk_widget_queue_draw(area);
    return GDK_EVENT_STOP;
}

static void on_graph_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data) {
    GraphState *state = user_data;
    state->drag_start_offset_x = state->offset_x;
    state->drag_start_offset_y = state->offset_y;
}

static void on_graph_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data) {
    GraphState *state = user_data;
    state->offset_x = state->drag_start_offset_x + offset_x;
    state->offset_y = state->drag_start_offset_y + offset_y;
    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_widget_queue_draw(area);
}

static void on_graph_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    ContainerObject *cobj = CONTAINER_OBJECT(user_data);
    const gchar *action_name = g_action_get_name(G_ACTION(action));
    
    g_print("DEBUG: on_graph_action called for '%s' on %s\n", action_name, cobj ? cobj->info->name : "NULL");
    
    GError *err = NULL;
    gboolean success = FALSE;
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
    
    // Set immediate visual feedback
    if (cobj && cobj->info && (g_strcmp0(action_name, "start") == 0 || g_strcmp0(action_name, "stop") == 0 || g_strcmp0(action_name, "restart") == 0)) {
        g_free(cobj->info->state);
        g_free(cobj->info->status);
        if (g_strcmp0(action_name, "start") == 0) {
            cobj->info->state = g_strdup("starting");
            cobj->info->status = g_strdup("Starting...");
        } else if (g_strcmp0(action_name, "stop") == 0) {
            cobj->info->state = g_strdup("exited");
            cobj->info->status = g_strdup("Stopping...");
        } else if (g_strcmp0(action_name, "restart") == 0) {
            cobj->info->state = g_strdup("restarting");
            cobj->info->status = g_strdup("Restarting...");
        }
        g_signal_emit_by_name(cobj, "stats-updated");
    }
    
    if (g_strcmp0(action_name, "start") == 0) {
        success = docker_client_start_container(cobj->info->id, &err);
    } else if (g_strcmp0(action_name, "stop") == 0) {
        success = docker_client_stop_container(cobj->info->id, &err);
    } else if (g_strcmp0(action_name, "restart") == 0) {
        success = docker_client_restart_container(cobj->info->id, &err);
    } else if (g_strcmp0(action_name, "remove") == 0) {
        success = docker_client_remove_container(cobj->info->id, &err);
    } else if (g_strcmp0(action_name, "inspect") == 0) {
        show_container_modal(cobj->info, GTK_WIDGET(window));
        return;
    }
    
    if (!success && err) {
        GtkWidget *dialog = gtk_message_dialog_new(window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Action '%s' failed on %s", action_name, cobj->info->name);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", err->message);
        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_window_present(GTK_WINDOW(dialog));
        g_error_free(err);
    } else if (success && cobj && cobj->info) {
        // If successful, we assume it's running (except for stop)
        g_free(cobj->info->state);
        g_free(cobj->info->status);
        if (g_strcmp0(action_name, "stop") == 0 || g_strcmp0(action_name, "remove") == 0) {
            cobj->info->state = g_strdup("exited");
            cobj->info->status = g_strdup("Exited");
        } else {
            cobj->info->state = g_strdup("running");
            cobj->info->status = g_strdup("Up");
        }
        g_signal_emit_by_name(cobj, "stats-updated");
    }
}

static gboolean destroy_popover_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover *popover, gpointer data) {
    g_idle_add(destroy_popover_idle, popover);
}

static void on_graph_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GraphState *state = user_data;
    if (!state->graph) return;
    
    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    
    for (Agnode_t *n = agfstnode(state->graph); n; n = agnxtnode(state->graph, n)) {
        double nx = ND_coord(n).x * state->scale + state->offset_x;
        double ny = ND_coord(n).y * state->scale + state->offset_y;
        double radius = 30 * state->scale;
        if (radius < 15) radius = 15;
        if (radius > 40) radius = 40;
        
        double dist = sqrt(pow(x - nx, 2) + pow(y - ny, 2));
        if (dist <= radius) {
            // Find container in store
            guint n_items = g_list_model_get_n_items(state->store);
            for (guint i = 0; i < n_items; i++) {
                ContainerObject *cobj = g_list_model_get_item(state->store, i);
                if (cobj && g_strcmp0(cobj->info->name, agnameof(n)) == 0) {
                    
                    GtkWidget *popover = gtk_popover_menu_new_from_model(NULL);
                    gtk_widget_set_parent(popover, area);
                    
                    GSimpleActionGroup *action_group = g_simple_action_group_new();
                    const gchar *actions[] = {"start", "stop", "restart"};
                    GMenu *menu = g_menu_new();
                    for (int j = 0; j < 3; j++) {
                        gchar *action_id = g_strdup_printf("c.%s", actions[j]);
                        gchar *label = g_strdup(actions[j]);
                        label[0] = g_ascii_toupper(label[0]);
                        g_menu_append(menu, label, action_id);
                        
                        GSimpleAction *action = g_simple_action_new(actions[j], NULL);
                        g_signal_connect(action, "activate", G_CALLBACK(on_graph_action), cobj);
                        g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
                        g_object_unref(action);
                        g_free(action_id);
                        g_free(label);
                    }
                    
                    gtk_widget_insert_action_group(popover, "c", G_ACTION_GROUP(action_group));
                    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(popover), G_MENU_MODEL(menu));
                    g_object_unref(menu);
                    g_object_unref(action_group);
                    
                    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
                    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
                    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
                    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
                    gtk_popover_popup(GTK_POPOVER(popover));
                    
                    g_object_unref(cobj);
                    break;
                }
                if (cobj) g_object_unref(cobj);
            }
            break;
        }
    }
}

static void on_graph_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    // Basic hit detection
    GraphState *state = user_data;
    if (!state->graph) return;
    for (Agnode_t *n = agfstnode(state->graph); n; n = agnxtnode(state->graph, n)) {
        double nx = ND_coord(n).x * state->scale + state->offset_x;
        double ny = ND_coord(n).y * state->scale + state->offset_y;
        double radius = 30 * state->scale;
        if (radius < 15) radius = 15;
        if (radius > 40) radius = 40;
        
        double dist = sqrt(pow(x - nx, 2) + pow(y - ny, 2));
        if (dist <= radius) {
            g_print("Clicked on node: %s\n", agnameof(n));
            
            if (g_str_has_prefix(agnameof(n), "net:")) {
                const gchar *net_name = agnameof(n) + 4; // Skip "net:"
                GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
                GtkWindow *window = GTK_WINDOW(gtk_widget_get_root(area));
                show_network_modal(net_name, GTK_WIDGET(window));
                break;
            }
            
            // Find container in store
            guint n_items = g_list_model_get_n_items(state->store);
            for (guint i = 0; i < n_items; i++) {
                ContainerObject *cobj = g_list_model_get_item(state->store, i);
                if (cobj && g_strcmp0(cobj->info->name, agnameof(n)) == 0) {
                    GtkWidget *area = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
                    GtkWindow *window = GTK_WINDOW(gtk_widget_get_root(area));
                    show_container_modal(cobj->info, GTK_WIDGET(window));
                    g_object_unref(cobj);
                    break;
                }
                if (cobj) g_object_unref(cobj);
            }
            break;
        }
    }
}

GtkWidget *create_graph_view(GListModel *store) {
    GraphState *state = g_new0(GraphState, 1);
    state->store = g_object_ref(store);
    state->user_zoom = 1.0;
    
    GtkWidget *area = gtk_drawing_area_new();
    g_object_set_data(G_OBJECT(area), "state", state);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, state, g_free);
    
    update_graph_layout(state);
    
    // Connect model changes
    g_signal_connect(store, "items-changed", G_CALLBACK(on_store_items_changed), area);
    
    // Click interaction
    GtkGestureClick *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_graph_click), state);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));
    
    // Right click interaction
    GtkGestureClick *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_graph_right_click), state);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(rclick));
    
    // Middle click pan (drag)
    GtkGesture *drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_MIDDLE);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_graph_drag_begin), state);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_graph_drag_update), state);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));
    
    // Scroll zoom
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_graph_scroll), state);
    gtk_widget_add_controller(area, scroll);
    
    // Setup a timer to periodically redraw and layout the graph if stats update
    g_timeout_add(5000, periodic_update_cb, area);
    
    // Timer for blinking animation (4 FPS)
    g_timeout_add(250, blink_timer_cb, area);
    
    return area;
}
