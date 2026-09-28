// docker_client.h – simple wrapper around Docker Engine REST API using libcurl + json-glib

#ifndef DOCKER_CLIENT_H
#define DOCKER_CLIENT_H

#include <glib.h>
#include <glib-object.h>

/* Structure representing a Docker container (minimal fields needed for UI) */
typedef struct {
    gchar *id;          // 64‑character container ID (short version is fine)
    gchar *name;        // Human‑readable name (first entry of Names array)
    gchar *image;       // Image name/tag used to create the container
    gchar *status;      // "running", "exited", etc.
    gchar *state;       // Detailed state string from Docker (e.g., "running")
    // Resource stats
    double cpu_percent;
    guint64 mem_usage;
    guint64 mem_limit;
    guint64 net_rx;
    guint64 net_tx;
    guint64 blk_read;
    guint64 blk_write;
    
    // History for sparkline graph
    double cpu_history[60];
    int history_idx;
    int history_count;
    
    // Relationships
    GList *networks; // List of gchar*
    GList *volumes;  // List of gchar*
} ContainerInfo;

/* Returns a GList of ContainerInfo* (the list owns the items). Caller must free with
   docker_client_free_container_list(). Returns NULL on error (and sets GError). */
GList *docker_client_list_containers(GError **error);

/* Frees a single ContainerInfo structure */
void docker_client_free_container(ContainerInfo *c);

void docker_client_free_container_list(GList *list);

gboolean docker_client_start_container(const gchar *id, GError **error);
gboolean docker_client_stop_container(const gchar *id, GError **error);
gboolean docker_client_restart_container(const gchar *id, GError **error);
gboolean docker_client_remove_container(const gchar *id, GError **error);

gchar* docker_client_get_logs(const gchar *id);
gchar* docker_client_inspect_network(const gchar *net_name);


// ContainerObject wrapper for GListStore
#define CONTAINER_TYPE_OBJECT (container_object_get_type())
G_DECLARE_FINAL_TYPE(ContainerObject, container_object, CONTAINER, OBJECT, GObject)

struct _ContainerObject {
    GObject parent_instance;
    ContainerInfo *info;
};

ContainerObject *container_object_new(ContainerInfo *info);

#endif /* DOCKER_CLIENT_H */
