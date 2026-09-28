// resource_monitor.c – periodic container resource monitoring implementation

#include "core/resource_monitor.h"
#include "core/docker_client.h"
#include <glib.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

typedef struct {
    ResourceMonitorCallback cb;
    void *user_data;
} MonitorData;

typedef struct {
    ResourceMonitorCallback cb;
    void *user_data;
    ContainerStats *stats;
} StatUpdateData;

static gboolean apply_stats_idle(gpointer user_data) {
    StatUpdateData *data = user_data;
    if (data->cb && data->stats) {
        data->cb(data->stats, data->user_data);
    }
    if (data->stats) {
        g_free(data->stats->id);
        g_free(data->stats);
    }
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_stats_thread(gpointer user_data) {
    MonitorData *md = (MonitorData *)user_data;
    if (!md) return NULL;
    
    GError *err = NULL;
    GList *containers = docker_client_list_containers(&err);
    if (containers) {
        for (GList *l = containers; l != NULL; l = l->next) {
            ContainerInfo *c = (ContainerInfo *)l->data;
            if (g_strcmp0(c->state, "running") == 0) {
                GError *err2 = NULL;
                ContainerStats *stats = fetch_container_stats(c->id, &err2);
                if (stats) {
                    StatUpdateData *data = g_new(StatUpdateData, 1);
                    data->cb = md->cb;
                    data->user_data = md->user_data;
                    data->stats = stats;
                    // Ensure the callback is executed in the main thread (useful for GUIs, and safe generally in GLib apps)
                    g_idle_add(apply_stats_idle, data);
                }
                if (err2) g_error_free(err2);
            }
        }
        docker_client_free_container_list(containers);
    }
    if (err) g_error_free(err);
    return NULL;
}

/* Periodic callback – called every 5 seconds */
static gboolean monitor_timeout_cb(gpointer user_data) {
    MonitorData *md = (MonitorData *)user_data;
    GThread *thread = g_thread_new("monitor_thread", fetch_stats_thread, md);
    g_thread_unref(thread);
    return G_SOURCE_CONTINUE; // keep the timeout
}

static void monitor_data_free(gpointer user_data) {
    g_free(user_data);
}

guint start_resource_monitor(ResourceMonitorCallback cb, void *user_data) {
    MonitorData *md = g_new(MonitorData, 1);
    md->cb = cb;
    md->user_data = user_data;
    // 5-second interval
    return g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 5, monitor_timeout_cb, md, monitor_data_free);
}

void stop_resource_monitor(guint source_id) {
    if (source_id != 0) {
        g_source_remove(source_id);
    }
}

static size_t monitor_curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total = size * nmemb;
    GString *stream = (GString *)userdata;
    g_string_append_len(stream, ptr, total);
    return total;
}

ContainerStats *fetch_container_stats(const gchar *container_id, GError **error) {
    if (!container_id) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "container_id is NULL");
        return NULL;
    }

    gchar *url = g_strdup_printf("http://localhost/v1.41/containers/%s/stats?stream=false", container_id);
    CURL *curl = curl_easy_init();
    if (!curl) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to init libcurl");
        g_free(url);
        return NULL;
    }
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    GString *stream = g_string_new(NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, monitor_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, stream);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "curl error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        g_free(url);
        g_string_free(stream, TRUE);
        return NULL;
    }
    curl_easy_cleanup(curl);
    g_free(url);
    const gchar *response = stream->str;

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, -1, error)) {
        g_string_free(stream, TRUE);
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);
    if (!obj) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid JSON from Docker stats");
        g_string_free(stream, TRUE);
        g_object_unref(parser);
        return NULL;
    }

    ContainerStats *stats = g_new0(ContainerStats, 1);
    stats->id = g_strdup(container_id);

    // Helper macros for robust JSON parsing
#define HAS_OBJ(o, k) (json_object_has_member((o), (k)) && JSON_NODE_HOLDS_OBJECT(json_object_get_member((o), (k))))
#define HAS_ARR(o, k) (json_object_has_member((o), (k)) && JSON_NODE_HOLDS_ARRAY(json_object_get_member((o), (k))))
#define HAS_INT(o, k) (json_object_has_member((o), (k)) && JSON_NODE_HOLDS_VALUE(json_object_get_member((o), (k))))

    // CPU percentage
    if (HAS_OBJ(obj, "cpu_stats") && HAS_OBJ(obj, "precpu_stats")) {
        JsonObject *cpu_stats = json_object_get_object_member(obj, "cpu_stats");
        JsonObject *precpu_stats = json_object_get_object_member(obj, "precpu_stats");
        if (HAS_OBJ(cpu_stats, "cpu_usage") && HAS_OBJ(precpu_stats, "cpu_usage")) {
            JsonObject *cpu_usage = json_object_get_object_member(cpu_stats, "cpu_usage");
            JsonObject *pre_cpu_usage = json_object_get_object_member(precpu_stats, "cpu_usage");
            if (HAS_INT(cpu_usage, "total_usage") && HAS_INT(pre_cpu_usage, "total_usage") &&
                HAS_INT(cpu_stats, "system_cpu_usage") && HAS_INT(precpu_stats, "system_cpu_usage")) {
                guint64 cpu_total = json_object_get_int_member(cpu_usage, "total_usage");
                guint64 pre_cpu_total = json_object_get_int_member(pre_cpu_usage, "total_usage");
                guint64 system_cpu = json_object_get_int_member(cpu_stats, "system_cpu_usage");
                guint64 pre_system_cpu = json_object_get_int_member(precpu_stats, "system_cpu_usage");
                if (system_cpu > pre_system_cpu) {
                    stats->cpu_percent = (double)(cpu_total - pre_cpu_total) * 100.0 / (double)(system_cpu - pre_system_cpu);
                }
            }
        }
    }

    // Memory usage and limit
    if (HAS_OBJ(obj, "memory_stats")) {
        JsonObject *memory_stats = json_object_get_object_member(obj, "memory_stats");
        if (HAS_INT(memory_stats, "usage")) {
            stats->mem_usage = json_object_get_int_member(memory_stats, "usage");
        }
        if (HAS_INT(memory_stats, "limit")) {
            stats->mem_limit = json_object_get_int_member(memory_stats, "limit");
        }
    }

    // Network I/O
    if (HAS_OBJ(obj, "networks")) {
        JsonObject *networks = json_object_get_object_member(obj, "networks");
        const gchar *iface;
        JsonObjectIter iter;
        json_object_iter_init(&iter, networks);
        while (json_object_iter_next(&iter, &iface, NULL)) {
            if (HAS_OBJ(networks, iface)) {
                JsonObject *iface_obj = json_object_get_object_member(networks, iface);
                if (HAS_INT(iface_obj, "rx_bytes")) {
                    stats->net_rx += json_object_get_int_member(iface_obj, "rx_bytes");
                }
                if (HAS_INT(iface_obj, "tx_bytes")) {
                    stats->net_tx += json_object_get_int_member(iface_obj, "tx_bytes");
                }
            }
        }
    }

    // Block I/O
    if (HAS_OBJ(obj, "blkio_stats")) {
        JsonObject *blkio_stats = json_object_get_object_member(obj, "blkio_stats");
        if (HAS_ARR(blkio_stats, "io_service_bytes_recursive")) {
            JsonArray *io_service_bytes = json_object_get_array_member(blkio_stats, "io_service_bytes_recursive");
            for (guint i = 0; i < json_array_get_length(io_service_bytes); ++i) {
                JsonNode *node = json_array_get_element(io_service_bytes, i);
                if (JSON_NODE_HOLDS_OBJECT(node)) {
                    JsonObject *entry = json_node_get_object(node);
                    if (json_object_has_member(entry, "op") && json_object_has_member(entry, "value")) {
                        const gchar *op = json_object_get_string_member(entry, "op");
                        guint64 value = json_object_get_int_member(entry, "value");
                        if (g_strcmp0(op, "Read") == 0) {
                            stats->blk_read += value;
                        } else if (g_strcmp0(op, "Write") == 0) {
                            stats->blk_write += value;
                        }
                    }
                }
            }
        }
    }

    g_string_free(stream, TRUE);
    g_object_unref(parser);
    return stats;
}
