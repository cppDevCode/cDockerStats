// resource_monitor.h – monitor container resource usage (CPU, memory, network, disk)

#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include <glib.h>
#include "core/docker_client.h"

/* Structure representing per‑container resource stats */
typedef struct {
    gchar *id;          // container id (matches ContainerInfo.id)
    double cpu_percent; // CPU usage percentage
    guint64 mem_usage;  // memory usage in bytes
    guint64 mem_limit;  // memory limit in bytes
    guint64 net_rx;     // network received bytes
    guint64 net_tx;     // network transmitted bytes
    guint64 blk_read;   // block I/O read bytes
    guint64 blk_write;  // block I/O write bytes
} ContainerStats;

typedef void (*ResourceMonitorCallback)(ContainerStats *stats, void *user_data);

/* Starts periodic monitoring (every 5 seconds). The callback updates UI.
   Returns a source ID that can be used with g_source_remove() to stop.
 */
guint start_resource_monitor(ResourceMonitorCallback cb, void *user_data);

/* Stop the periodic monitoring using the source ID returned by start_resource_monitor(). */
void stop_resource_monitor(guint source_id);

/* Fetch current stats for a single container (synchronous, for on‑demand use).
   Returns a newly allocated ContainerStats, or NULL on error (sets GError).
 */
ContainerStats *fetch_container_stats(const gchar *container_id, GError **error);

#endif /* RESOURCE_MONITOR_H */
