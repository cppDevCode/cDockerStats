#ifndef INFO_MODAL_H
#define INFO_MODAL_H

#include <gtk/gtk.h>
#include "core/docker_client.h"

void show_container_modal(ContainerInfo *info, GtkWidget *parent);
void show_network_modal(const gchar *net_name, GtkWidget *parent);

#endif // INFO_MODAL_H
