#include <gtk/gtk.h>

static void on_action(GSimpleAction *action, GVariant *param, gpointer data) {
    g_print("ACTION FIRED!\n");
}

static gboolean destroy_popover_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

static void on_closed(GtkPopover *popover, gpointer data) {
    g_idle_add(destroy_popover_idle, popover);
}

static void on_click(GtkGestureClick *g, int n, double x, double y, gpointer data) {
    GtkWidget *box = data;
    GtkWidget *popover = gtk_popover_menu_new_from_model(NULL);
    gtk_widget_set_parent(popover, box);

    GSimpleActionGroup *group = g_simple_action_group_new();
    GSimpleAction *action = g_simple_action_new("start", NULL);
    g_signal_connect(action, "activate", G_CALLBACK(on_action), NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
    g_object_unref(action);

    gtk_widget_insert_action_group(popover, "c", G_ACTION_GROUP(group));
    g_object_unref(group);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Start", "c.start");
    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(popover), G_MENU_MODEL(menu));
    g_object_unref(menu);

    g_signal_connect(popover, "closed", G_CALLBACK(on_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void activate(GtkApplication *app) {
    GtkWidget *win = gtk_application_window_new(app);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *lbl = gtk_label_new("Right click me!");
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkGestureClick *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), box);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));

    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv) {
    g_setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    GtkApplication *app = gtk_application_new("test.popover", 0);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
