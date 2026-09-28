#ifndef GAUGE_VIEW_H
#define GAUGE_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkWidget *gauge_view_new(const char *title);
void gauge_view_set_value(GtkWidget *widget, double percentage, const char *text, const char *min_text, const char *max_text);

G_END_DECLS

#endif // GAUGE_VIEW_H
