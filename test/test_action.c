#include <gtk/gtk.h>
static void on_activate(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    g_print("Action activated! %s\n", (char*)user_data);
}
int main(int argc, char **argv) {
    gtk_init();
    GSimpleActionGroup *group = g_simple_action_group_new();
    GSimpleAction *action = g_simple_action_new("start", NULL);
    g_signal_connect(action, "activate", G_CALLBACK(on_activate), "hello");
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
    g_object_unref(action);
    g_action_group_activate_action(G_ACTION_GROUP(group), "start", NULL);
    return 0;
}
