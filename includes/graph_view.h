// graph_view.h – stub for container graph view using Graphviz

#ifndef GRAPH_VIEW_H
#define GRAPH_VIEW_H

#include <gtk/gtk.h>

/* Create the container graph view widget.
   This returns a GtkWidget (e.g., a GtkDrawingArea or ScrolledWindow)
   that can be embedded in a split view.
 */
GtkWidget *create_graph_view(GListModel *store);

#endif // GRAPH_VIEW_H
