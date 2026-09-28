// main.c – entry point for cDockerStats
// Minimal GTK4 window that will later host the full UI.

#include <gtk/gtk.h>
#include "core/resource_monitor.h"
#include "cDockerStats-gtk4/graph_view.h"
#include "core/docker_client.h"
#include "cDockerStats-gtk4/container_object.h"
#include <unistd.h>
#include "cDockerStats-gtk4/info_modal.h"
#include "cDockerStats-gtk4/gauge_view.h"

// Forward declaration of UI initialization (to be implemented later)
void init_ui(GtkApplication *app);

// Column identifiers for readability (no longer used with GtkListView)
enum {
    COL_ID = 0,
    COL_NAME,
    COL_IMAGE,
    COL_STATUS,
    COL_STATE,
    COL_CPU,
    COL_MEM,
    COL_NET,
    COL_DISK,
    NUM_COLS
};

G_DEFINE_TYPE(ContainerObject, container_object, G_TYPE_OBJECT)

static void container_object_init(ContainerObject *self) {}
static void container_object_finalize(GObject *object) {
    ContainerObject *self = CONTAINER_OBJECT(object);
    if (self->info) {
        docker_client_free_container(self->info);
    }
    G_OBJECT_CLASS(container_object_parent_class)->finalize(object);
}

enum {
    SIG_STATS_UPDATED,
    LAST_SIGNAL
};
static guint container_signals[LAST_SIGNAL] = { 0 };

static void container_object_class_init(ContainerObjectClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = container_object_finalize;
    container_signals[SIG_STATS_UPDATED] = g_signal_new("stats-updated",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL,
        G_TYPE_NONE, 0);
}

ContainerObject *container_object_new(ContainerInfo *info) {
    ContainerObject *self = g_object_new(CONTAINER_TYPE_OBJECT, NULL);
    self->info = info;
    return self;
}

typedef struct UIData {
    GListStore *store;
    GtkSingleSelection *selection;
    GtkWidget *window;
    guint monitor_source_id;
    GtkWidget *gauge_cpu;
    GtkWidget *gauge_mem;
    GtkWidget *gauge_net;
} UIData;

static void load_containers_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void load_containers_done(GObject *source_object, GAsyncResult *res, gpointer user_data);
// Forward declarations for factory callbacks
static void on_factory_setup(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data);
static void on_factory_bind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data);
static void on_selection_changed(GtkSingleSelection *selection, GParamSpec *pspec, gpointer user_data);
static void on_item_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
static void on_factory_unbind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data);

GtkSizeGroup *col_sg[NUM_COLS];

int main(int argc, char **argv) {
  // Initialize size groups
  for (int i = 0; i < NUM_COLS; i++) {
      col_sg[i] = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  }

  // Silence libEGL/DRI3 hardware acceleration warnings by falling back to software rendering
  g_setenv("LIBGL_ALWAYS_SOFTWARE", "1", FALSE);

  // Create a GtkApplication with unique application ID
  GtkApplication *app = gtk_application_new("com.example.cDockerStats",
                                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(init_ui), NULL);

  // Demo: list containers and print count to stdout
  GError *err = NULL;
  GList *containers = docker_client_list_containers(&err);
  if (containers) {
      g_print("Docker containers found: %u\n", g_list_length(containers));
      docker_client_free_container_list(containers);
  } else {
      g_print("Error listing containers: %s\n", err->message);
      g_error_free(err);
  }

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}

static gboolean update_global_gauges_cb(gpointer user_data) {
    UIData *ui = user_data;
    if (!ui->store) return G_SOURCE_CONTINUE;

    guint n = g_list_model_get_n_items(G_LIST_MODEL(ui->store));
    double total_cpu = 0.0;
    guint64 total_mem_usage = 0;
    guint64 total_mem_limit = 0;
    guint64 total_net = 0;

    for (guint i = 0; i < n; i++) {
        ContainerObject *cobj = g_list_model_get_item(G_LIST_MODEL(ui->store), i);
        if (cobj && cobj->info && g_strcmp0(cobj->info->state, "running") == 0) {
            total_cpu += cobj->info->cpu_percent;
            total_mem_usage += cobj->info->mem_usage;
            total_mem_limit += cobj->info->mem_limit;
            total_net += cobj->info->net_rx + cobj->info->net_tx;
        }
        if (cobj) g_object_unref(cobj);
    }

    gchar *cpu_text = g_strdup_printf("%.1f%%", total_cpu);
    double cpu_frac = MIN(1.0, total_cpu / 100.0);
    gauge_view_set_value(ui->gauge_cpu, cpu_frac, cpu_text, "0%", "100%");
    g_free(cpu_text);

    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    guint64 host_mem = (guint64)pages * (guint64)page_size;

    double mem_frac = 0.0;
    if (host_mem > 0) mem_frac = (double)total_mem_usage / (double)host_mem;
    gchar *mem_text = g_strdup_printf("%.2f GB", (double)total_mem_usage / (1024.0*1024.0*1024.0));
    gchar *mem_max_text = g_strdup_printf("%.1f GB", (double)host_mem / (1024.0*1024.0*1024.0));
    gauge_view_set_value(ui->gauge_mem, mem_frac, mem_text, "0GB", mem_max_text);
    g_free(mem_text);
    g_free(mem_max_text);

    static guint64 prev_total_net = 0;
    static gboolean first_net = TRUE;
    double net_rate = 0.0;
    if (!first_net && total_net >= prev_total_net) {
        net_rate = (total_net - prev_total_net) / 2.0; // updated every 2s
    }
    prev_total_net = total_net;
    first_net = FALSE;

    double net_frac = MIN(1.0, net_rate / (10.0 * 1024.0 * 1024.0)); // 10MB/s max scale
    gchar *net_text = g_strdup_printf("%.2f MB/s", net_rate / (1024.0*1024.0));
    gauge_view_set_value(ui->gauge_net, net_frac, net_text, "0MB/s", "10MB/s");
    g_free(net_text);

    return G_SOURCE_CONTINUE;
}

static void on_core_stats_updated(ContainerStats *stats, void *user_data) {
    UIData *ui = user_data;
    if (!ui || !ui->store) return;
    
    guint n = g_list_model_get_n_items(G_LIST_MODEL(ui->store));
    for (guint i = 0; i < n; i++) {
        ContainerObject *cobj = g_list_model_get_item(G_LIST_MODEL(ui->store), i);
        if (cobj && cobj->info && g_strcmp0(cobj->info->id, stats->id) == 0) {
            cobj->info->cpu_percent = stats->cpu_percent;
            cobj->info->mem_usage = stats->mem_usage;
            cobj->info->mem_limit = stats->mem_limit;
            cobj->info->net_rx = stats->net_rx;
            cobj->info->net_tx = stats->net_tx;
            cobj->info->blk_read = stats->blk_read;
            cobj->info->blk_write = stats->blk_write;

            int idx = cobj->info->history_idx;
            cobj->info->cpu_history[idx] = stats->cpu_percent;
            cobj->info->history_idx = (idx + 1) % 60;
            if (cobj->info->history_count < 60) {
                cobj->info->history_count++;
            }

            g_signal_emit_by_name(cobj, "stats-updated");
            g_object_unref(cobj);
            break;
        }
        if (cobj) g_object_unref(cobj);
    }
}

// Placeholder UI initialization – currently just opens an empty window.
void init_ui(GtkApplication *app) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window),
                       "cDockerStats – Docker monitor (GTK4)");
  gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

  /* --- Initialize UIData with store and selection --- */
  UIData *ui = g_new0(UIData, 1);
  GListStore *store = g_list_store_new(CONTAINER_TYPE_OBJECT);
  ui->store = store;
  GtkSingleSelection *selection = gtk_single_selection_new(G_LIST_MODEL(store));
  ui->selection = selection;
  ui->window = window;
  // Start the periodic resource monitor (5 s interval)
  ui->monitor_source_id = start_resource_monitor(on_core_stats_updated, ui);

  // Start async task to load containers
  GTask *task = g_task_new(NULL, NULL, load_containers_done, ui);
  g_task_set_source_tag(task, "load_containers");
  g_task_run_in_thread(task, load_containers_thread);
  g_object_unref(task);

  /* Create TreeView */
  // Create a ListView with a custom factory to display container fields
GtkListItemFactory *factory = gtk_signal_list_item_factory_new();

g_signal_connect(factory, "setup", G_CALLBACK(on_factory_setup), NULL);

g_signal_connect(factory, "bind", G_CALLBACK(on_factory_bind), NULL);



  GtkWidget *list = gtk_list_view_new(GTK_SELECTION_MODEL(ui->selection), (GtkListItemFactory *)factory);
  g_signal_connect(factory, "unbind", G_CALLBACK(on_factory_unbind), NULL);
  // factory already set via constructor, no need to call set_factory again

// Add a simple header row using a Box with bold labels
GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
const char *titles[NUM_COLS] = {"ID", "Name", "Image", "Status", "State", "CPU %", "Memory", "Net I/O", "Disk I/O"};
for (int i = 0; i < NUM_COLS; ++i) {
    GtkWidget *lbl = gtk_label_new(titles[i]);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.5); // Center align header
    gtk_size_group_add_widget(col_sg[i], lbl);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "title");
    gtk_box_append(GTK_BOX(header), lbl);
}
// Pack header above the list
GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
gtk_box_append(GTK_BOX(vbox), header);
gtk_box_append(GTK_BOX(vbox), list);

GtkWidget *scrolled = gtk_scrolled_window_new();
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                              GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vbox);



  /* Create a Paned layout for Split View */
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child(GTK_PANED(paned), scrolled);
  
  /* Add Graph View to the right side */
  GtkWidget *graph_area = create_graph_view(G_LIST_MODEL(ui->store));
  gtk_paned_set_end_child(GTK_PANED(paned), graph_area);
  
  /* Give the list 450px by default */
  gtk_paned_set_position(GTK_PANED(paned), 450);

  /* Create top panel with gauges */
  GtkWidget *top_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
  gtk_widget_set_halign(top_panel, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(top_panel, 10);
  gtk_widget_set_margin_bottom(top_panel, 10);
  
  ui->gauge_cpu = gauge_view_new("Global CPU");
  ui->gauge_mem = gauge_view_new("Global Memory");
  ui->gauge_net = gauge_view_new("Global Net I/O");
  
  gtk_box_append(GTK_BOX(top_panel), ui->gauge_cpu);
  gtk_box_append(GTK_BOX(top_panel), ui->gauge_mem);
  gtk_box_append(GTK_BOX(top_panel), ui->gauge_net);
  
  // Start the gauge update timer
  g_timeout_add_seconds(2, update_global_gauges_cb, ui);

  /* Set the main layout as vertical box */
  GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(main_vbox), top_panel);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(main_vbox), paned);

  /* Set the main vbox as the main child of the window */
  gtk_window_set_child(GTK_WINDOW(window), main_vbox);

  // Load CSS for styling
  GtkCssProvider *css = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css,
    "list { background: #1e1e1e; color: #e0e0e0; }\n"
    ".title { font-weight: bold; background: #2c2c2c; color: #ffffff; }\n"
    "label { padding: 2px 4px; }\n"
    ".gauge-value { font-weight: bold; font-size: 14pt; color: #333333; }",
    -1);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                            GTK_STYLE_PROVIDER(css),
                                            GTK_STYLE_PROVIDER_PRIORITY_USER);

  // Connect selection change signal
  g_signal_connect(ui->selection, "selection-changed", G_CALLBACK(on_selection_changed), ui);
  // Remove list-level right_click controller; we will add it per-row in on_factory_setup

  gtk_window_present(GTK_WINDOW(window));
}

static void on_selection_changed(GtkSingleSelection *selection, GParamSpec *pspec, gpointer user_data) {
    guint pos = gtk_single_selection_get_selected(selection);
    if (pos == GTK_INVALID_LIST_POSITION) return;
    GListModel *model = gtk_single_selection_get_model(selection);
    ContainerObject *cobj = g_list_model_get_item(model, pos);
    if (cobj) {
        g_print("Selected container: %s (%s)\n", cobj->info->name, cobj->info->id);
        g_object_unref(cobj);
    }
}

static void on_context_menu_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    ContainerObject *cobj = CONTAINER_OBJECT(user_data);
    const gchar *action_name = g_action_get_name(G_ACTION(action));
    
    g_print("DEBUG: on_context_menu_action called for '%s' on %s\n", action_name, cobj ? cobj->info->name : "NULL");
    
    GError *err = NULL;
    gboolean success = FALSE;
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
    
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
    } else {
        return;
    }
    
    if (success) {
        if (cobj && cobj->info) {
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
    } else {
        GtkWidget *dialog = gtk_message_dialog_new(window, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Action '%s' failed on %s", action_name, cobj->info->name);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", err ? err->message : "Unknown error");
        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_window_present(GTK_WINDOW(dialog));
        if (err) g_error_free(err);
    }
}

static gboolean destroy_popover_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover *popover, gpointer data) {
    g_idle_add(destroy_popover_idle, popover);
}

static void on_item_right_click(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GtkWidget *box = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    ContainerObject *cobj = g_object_get_data(G_OBJECT(box), "cobj");
    if (!cobj) return;

    // Create a new PopoverMenu
    GtkWidget *popover = gtk_popover_menu_new_from_model(NULL);
    gtk_widget_set_parent(popover, box);
    
    // Create an action group for this menu
    GSimpleActionGroup *action_group = g_simple_action_group_new();
    
    const gchar *actions[] = {"start", "stop", "restart", "remove", "inspect", "exec"};
    GMenu *menu = g_menu_new();
    for (int i = 0; i < 6; i++) {
        gchar *action_id = g_strdup_printf("c.%s", actions[i]);
        gchar *label = g_strdup(actions[i]);
        label[0] = g_ascii_toupper(label[0]); // Capitalize
        
        g_menu_append(menu, label, action_id);
        
        GSimpleAction *action = g_simple_action_new(actions[i], NULL);
        g_signal_connect(action, "activate", G_CALLBACK(on_context_menu_action), cobj);
        g_action_map_add_action(G_ACTION_MAP(action_group), G_ACTION(action));
        g_object_unref(action);
        
        g_free(action_id);
        g_free(label);
    }
    
    gtk_widget_insert_action_group(popover, "c", G_ACTION_GROUP(action_group));
    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(popover), G_MENU_MODEL(menu));
    g_object_unref(menu);
    g_object_unref(action_group);

    // Set pointing to the click coordinates
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    
    // Auto-destroy the popover when closed using idle to allow actions to fire
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_factory_setup(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    
    // Add gesture for context menu on this row
    GtkGestureClick *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_item_right_click), NULL);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(right_click));

    for (int i = 0; i < NUM_COLS; ++i) {
        GtkWidget *lbl = gtk_label_new("");
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END); // Ellipsize long text like Image or Name
        gtk_size_group_add_widget(col_sg[i], lbl);
        gtk_box_append(GTK_BOX(box), lbl);
    }
    // Attach the box as the child of the list item
    gtk_list_item_set_child(item, box);
}

static void update_row_labels(ContainerInfo *c, GtkWidget *box) {
    if (!c || !box) return;
    GtkWidget *lbl = gtk_widget_get_first_child(box);
    GtkWidget *lbl_id = lbl;
    GtkWidget *lbl_name = gtk_widget_get_next_sibling(lbl_id);
    GtkWidget *lbl_image = gtk_widget_get_next_sibling(lbl_name);
    GtkWidget *lbl_status = gtk_widget_get_next_sibling(lbl_image);
    GtkWidget *lbl_state = gtk_widget_get_next_sibling(lbl_status);
    GtkWidget *lbl_cpu = gtk_widget_get_next_sibling(lbl_state);
    GtkWidget *lbl_mem = gtk_widget_get_next_sibling(lbl_cpu);
    GtkWidget *lbl_net = gtk_widget_get_next_sibling(lbl_mem);
    GtkWidget *lbl_disk = gtk_widget_get_next_sibling(lbl_net);

    gchar *short_id = c->id ? g_strndup(c->id, 12) : g_strdup("");
    gtk_label_set_text(GTK_LABEL(lbl_id), short_id);
    g_free(short_id);
    gtk_label_set_text(GTK_LABEL(lbl_name), c->name ? c->name : "");
    gtk_label_set_text(GTK_LABEL(lbl_image), c->image ? c->image : "");
    gtk_label_set_text(GTK_LABEL(lbl_status), c->status ? c->status : "");
    gtk_label_set_text(GTK_LABEL(lbl_state), c->state ? c->state : "");

    gchar *cpu_str = g_strdup_printf("%.2f%%", c->cpu_percent);
    gtk_label_set_text(GTK_LABEL(lbl_cpu), cpu_str);
    g_free(cpu_str);

    gchar *mem_str = g_strdup_printf("%" G_GUINT64_FORMAT "M / %" G_GUINT64_FORMAT "M", c->mem_usage / (1024*1024), c->mem_limit / (1024*1024));
    gtk_label_set_text(GTK_LABEL(lbl_mem), mem_str);
    g_free(mem_str);

    gchar *net_str = g_strdup_printf("%" G_GUINT64_FORMAT "kB↑ / %" G_GUINT64_FORMAT "kB↓", c->net_tx / 1024, c->net_rx / 1024);
    gtk_label_set_text(GTK_LABEL(lbl_net), net_str);
    g_free(net_str);

    gchar *disk_str = g_strdup_printf("%" G_GUINT64_FORMAT "kB↑ / %" G_GUINT64_FORMAT "kB↓", c->blk_write / 1024, c->blk_read / 1024);
    gtk_label_set_text(GTK_LABEL(lbl_disk), disk_str);
    g_free(disk_str);
}

static void on_stats_updated(ContainerObject *cobj, gpointer user_data) {
    GtkWidget *box = GTK_WIDGET(user_data);
    update_row_labels(cobj->info, box);
}

static void on_factory_bind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    ContainerObject *cobj = (ContainerObject *)gtk_list_item_get_item(item);
    if (!cobj || !cobj->info) return;
    GtkWidget *box = gtk_list_item_get_child(item);
    
    // Store reference to ContainerObject inside the box for context menu handling
    g_object_set_data(G_OBJECT(box), "cobj", cobj);
    
    // Initial update
    update_row_labels(cobj->info, box);
    
    // Connect signal to update when stats change
    g_signal_connect(cobj, "stats-updated", G_CALLBACK(on_stats_updated), box);
}

static void on_factory_unbind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    ContainerObject *cobj = (ContainerObject *)gtk_list_item_get_item(item);
    GtkWidget *box = gtk_list_item_get_child(item);
    if (cobj && box) {
        g_object_set_data(G_OBJECT(box), "cobj", NULL);
        g_signal_handlers_disconnect_by_func(cobj, G_CALLBACK(on_stats_updated), box);
    }
}



/* Async task to load containers */
static void load_containers_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    GError *err = NULL;
    GList *containers = docker_client_list_containers(&err);
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        if (containers)
            docker_client_free_container_list(containers);
        g_task_return_error(task, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"));
        return;
    }
    if (containers) {
        g_task_return_pointer(task, containers, (GDestroyNotify)docker_client_free_container_list);
    } else {
        g_task_return_error(task, err);
    }
}

static void load_containers_done(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    UIData *ui = (UIData *)user_data;
    GError *err = NULL;
    GList *containers = g_task_propagate_pointer(G_TASK(res), &err);
    if (containers) {
        // Clear existing items
        g_list_store_remove_all(ui->store);
        for (GList *l = containers; l != NULL; l = l->next) {
            ContainerInfo *c = (ContainerInfo *)l->data;
            ContainerObject *cobj = container_object_new(c);
            g_list_store_append(ui->store, cobj);
            g_object_unref(cobj);
        }
        // GListStore now owns the ContainerObjects which own the ContainerInfos.
        // We only free the list links.
        g_list_free(containers);
    } else {
        g_print("Error loading containers async: %s\n", err->message);
        g_error_free(err);
    }
}
