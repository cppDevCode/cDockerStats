// resource_monitor.c – periodic container resource monitoring implementation

#include "resource_monitor.h"
#include "docker_client.h"
#include <glib.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

/* Internal helper to fetch stats for a container and emit a signal or update UI.
   For now we simply print the stats to stdout. */
typedef struct {
    ContainerObject *cobj;
    ContainerStats *stats;
} StatUpdateData;

static gboolean apply_stats_idle(gpointer user_data) {
    StatUpdateData *data = user_data;
    if (data->cobj && data->cobj->info && data->stats) {
        data->cobj->info->cpu_percent = data->stats->cpu_percent;
        data->cobj->info->mem_usage = data->stats->mem_usage;
        data->cobj->info->mem_limit = data->stats->mem_limit;
        data->cobj->info->net_rx = data->stats->net_rx;
        data->cobj->info->net_tx = data->stats->net_tx;
        data->cobj->info->blk_read = data->stats->blk_read;
        data->cobj->info->blk_write = data->stats->blk_write;

        // Update history buffer
        int idx = data->cobj->info->history_idx;
        data->cobj->info->cpu_history[idx] = data->stats->cpu_percent;
        data->cobj->info->history_idx = (idx + 1) % 60;
        if (data->cobj->info->history_count < 60) {
            data->cobj->info->history_count++;
        }

        g_signal_emit_by_name(data->cobj, "stats-updated");
    }
    if (data->stats) {
        g_free(data->stats->id);
        g_free(data->stats);
    }
    if (data->cobj) g_object_unref(data->cobj);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static gpointer fetch_stats_thread(gpointer user_data) {
    GListStore *store = (GListStore *)user_data;
    if (!store) return NULL;
    
    guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
    for (guint i = 0; i < n; i++) {
        ContainerObject *cobj = g_list_model_get_item(G_LIST_MODEL(store), i);
        if (cobj && cobj->info) {
            GError *err = NULL;
            ContainerStats *stats = fetch_container_stats(cobj->info->id, &err);
            if (stats) {
                StatUpdateData *data = g_new(StatUpdateData, 1);
                data->cobj = g_object_ref(cobj);
                data->stats = stats;
                g_idle_add(apply_stats_idle, data);
            }
            if (err) g_error_free(err);
        }
        if (cobj) g_object_unref(cobj);
    }
    g_object_unref(store);
    return NULL;
}

/* Periodic callback – called every 5 seconds */
static gboolean monitor_timeout_cb(gpointer user_data) {
    GListStore *store = (GListStore *)user_data;
    GThread *thread = g_thread_new("monitor_thread", fetch_stats_thread, g_object_ref(store));
    g_thread_unref(thread);
    return G_SOURCE_CONTINUE; // keep the timeout
}

guint start_resource_monitor(GListStore *store) {
    // 5‑second interval (you may change the interval later)
    return g_timeout_add_seconds(5, monitor_timeout_cb, store);
}

void stop_resource_monitor(guint source_id) {
    if (source_id != 0) {
        g_source_remove(source_id);
    }
}

/* Fetch current stats for a single container (synchronous). This is a thin wrapper
   around Docker's "/containers/<id>/stats?stream=false" endpoint.
   It uses libcurl (through docker_client) and json‑glib to parse the response.
   The implementation extracts the fields defined in ContainerStats.
   Errors are reported via GError. */
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
