// src/docker_client.c – wrapper sencillo sobre la API REST de Docker
// Usa libcurl + json-glib para obtener la lista de contenedores.
// Esta versión solo implementa la lectura; las operaciones de gestión (start/stop, …) se añadirán más adelante.

#include "core/docker_client.h"
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Helper: almacenar el cuerpo de la respuesta HTTP ---------- */
typedef struct {
    GByteArray *data;   // buffer dinámico de bytes
} CurlResponse;

/* Callback de libcurl que acumula los datos recibidos */
static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userp) {
    CurlResponse *resp = (CurlResponse *)userp;
    size_t total = size * nmemb;
    g_byte_array_append(resp->data, (const guint8 *)ptr, total);
    return total;
}

/* ---------- Funciones públicas declaradas en docker_client.h ---------- */

/* Libera un ContainerInfo individual */
void docker_client_free_container(ContainerInfo *c) {
    if (!c) return;
    g_free(c->id);
    g_free(c->name);
    g_free(c->image);
    g_free(c->status);
    g_free(c->state);
    if (c->networks) g_list_free_full(c->networks, g_free);
    if (c->volumes) g_list_free_full(c->volumes, g_free);
    g_free(c);
}

/* Libera la lista completa devuelta por docker_client_list_containers() */
void docker_client_free_container_list(GList *list) {
    g_list_free_full(list, (GDestroyNotify)docker_client_free_container);
}

/* Obtiene la lista de contenedores (versión simplificada) */
GList *docker_client_list_containers(GError **error) {
    CURL *curl = NULL;
    CURLcode res;
    CurlResponse resp = {.data = g_byte_array_new()};

    /* Preparar libcurl */
    curl = curl_easy_init();
    if (!curl) {
        g_set_error(error, g_quark_from_string("DockerClient"), 1,
                    "Failed to initialise libcurl");
        g_byte_array_unref(resp.data);
        return NULL;
    }

    /* Docker escucha en el socket Unix. La URL “http://localhost/…“ funciona siempre que
       se indique el socket con CURLOPT_UNIX_SOCKET_PATH. */
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost/containers/json?all=1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);  // error si código HTTP >=400

    /* Ejecutar la petición */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        g_set_error(error, g_quark_from_string("DockerClient"), 2,
                    "curl error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        g_byte_array_unref(resp.data);
        return NULL;
    }

    /* Parsear JSON con json‑glib */
    JsonParser *parser = json_parser_new();
    GError *jerr = NULL;
    if (!json_parser_load_from_data(parser,
                                    (const gchar *)resp.data->data,
                                    resp.data->len,
                                    &jerr)) {
        g_set_error(error, g_quark_from_string("DockerClient"), 3,
                    "JSON parse error: %s", jerr->message);
        g_error_free(jerr);
        g_object_unref(parser);
        curl_easy_cleanup(curl);
        g_byte_array_unref(resp.data);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root)) {
        g_set_error(error, g_quark_from_string("DockerClient"), 4,
                    "Unexpected JSON format: root is not an array");
        g_object_unref(parser);
        curl_easy_cleanup(curl);
        g_byte_array_unref(resp.data);
        return NULL;
    }

    JsonArray *arr = json_node_get_array(root);
    GList *list = NULL;

    for (guint i = 0; i < json_array_get_length(arr); ++i) {
        JsonObject *obj = json_array_get_object_element(arr, i);
        ContainerInfo *ci = g_new0(ContainerInfo, 1);

        /* Campos de interés */
        ci->id = g_strdup(json_object_get_string_member(obj, "Id"));

        /* “Names” es un array de strings; tomamos el primero (sin '/' inicial) */
        JsonArray *names_arr = json_object_get_array_member(obj, "Names");
        if (names_arr && json_array_get_length(names_arr) > 0) {
            const gchar *raw = json_array_get_string_element(names_arr, 0);
            ci->name = g_strdup(raw[0] == '/' ? raw + 1 : raw);
        }

        ci->image  = g_strdup(json_object_get_string_member(obj, "Image"));
        ci->status = g_strdup(json_object_get_string_member(obj, "Status"));
        ci->state  = g_strdup(json_object_get_string_member(obj, "State"));

        /* Networks */
        if (json_object_has_member(obj, "NetworkSettings")) {
            JsonObject *ns = json_object_get_object_member(obj, "NetworkSettings");
            if (ns && json_object_has_member(ns, "Networks")) {
                JsonObject *nets = json_object_get_object_member(ns, "Networks");
                if (nets) {
                    GList *keys = json_object_get_members(nets);
                    for (GList *l = keys; l != NULL; l = l->next) {
                        ci->networks = g_list_append(ci->networks, g_strdup((gchar *)l->data));
                    }
                    if (keys) g_list_free(keys);
                }
            }
        }
        
        /* Fallback for stopped/restarting containers where Networks map is empty */
        if (!ci->networks && json_object_has_member(obj, "HostConfig")) {
            JsonObject *hc = json_object_get_object_member(obj, "HostConfig");
            if (hc && json_object_has_member(hc, "NetworkMode")) {
                const gchar *net_mode = json_object_get_string_member(hc, "NetworkMode");
                if (net_mode && g_strcmp0(net_mode, "default") != 0 && g_strcmp0(net_mode, "none") != 0 && !g_str_has_prefix(net_mode, "container:")) {
                    ci->networks = g_list_append(ci->networks, g_strdup(net_mode));
                }
            }
        }

        /* Mounts (Volumes) */
        if (json_object_has_member(obj, "Mounts")) {
            JsonArray *mounts = json_object_get_array_member(obj, "Mounts");
            if (mounts) {
                for (guint j = 0; j < json_array_get_length(mounts); j++) {
                    JsonObject *mnt = json_array_get_object_element(mounts, j);
                    const gchar *mnt_name = NULL;
                    if (json_object_has_member(mnt, "Name")) {
                        mnt_name = json_object_get_string_member(mnt, "Name");
                    } else if (json_object_has_member(mnt, "Source")) {
                        mnt_name = json_object_get_string_member(mnt, "Source");
                    }
                    if (mnt_name) {
                        // Truncate if it's a long hash path
                        gchar *short_mnt = g_strdup(mnt_name);
                        if (strlen(short_mnt) > 15 && short_mnt[0] != '/') {
                            short_mnt[12] = '.'; short_mnt[13] = '.'; short_mnt[14] = '.'; short_mnt[15] = '\0';
                        }
                        ci->volumes = g_list_append(ci->volumes, short_mnt);
                    }
                }
            }
        }

        list = g_list_append(list, ci);
    }

    /* Cleanup */
    g_object_unref(parser);
    curl_easy_cleanup(curl);
    g_byte_array_unref(resp.data);

    return list;   // El llamador debe liberar con docker_client_free_container_list()
}

static gboolean docker_client_action(const gchar *id, const gchar *action, const gchar *method, GError **error) {
    if (!id) return FALSE;
    gchar *url;
    if (g_strcmp0(action, "remove") == 0) {
        url = g_strdup_printf("http://localhost/v1.41/containers/%s?force=true", id);
    } else {
        url = g_strdup_printf("http://localhost/v1.41/containers/%s/%s", id, action);
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to init libcurl");
        g_free(url);
        return FALSE;
    }
    
    CurlResponse resp = {.data = g_byte_array_new()};
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    gboolean success = (res == CURLE_OK && http_code >= 200 && http_code < 300);
    if (!success) {
        gchar *err_msg = NULL;
        if (resp.data->len > 0) {
            g_byte_array_append(resp.data, (guint8*)"\0", 1);
            err_msg = g_strdup((gchar*)resp.data->data);
        } else {
            err_msg = g_strdup_printf("Action failed with HTTP %ld", http_code);
        }
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", err_msg);
        g_free(err_msg);
    }
    
    g_byte_array_unref(resp.data);
    curl_easy_cleanup(curl);
    g_free(url);
    return success;
}

gboolean docker_client_start_container(const gchar *id, GError **error) {
    return docker_client_action(id, "start", "POST", error);
}

gboolean docker_client_stop_container(const gchar *id, GError **error) {
    return docker_client_action(id, "stop", "POST", error);
}

gboolean docker_client_restart_container(const gchar *id, GError **error) {
    return docker_client_action(id, "restart", "POST", error);
}

gboolean docker_client_remove_container(const gchar *id, GError **error) {
    return docker_client_action(id, "remove", "DELETE", error);
}

gchar* docker_client_get_logs(const gchar *id) {
    if (!id) return NULL;
    gchar *url = g_strdup_printf("http://localhost/v1.41/containers/%s/logs?stdout=true&stderr=true&tail=50", id);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        g_free(url);
        return NULL;
    }
    
    CurlResponse resp = {.data = g_byte_array_new()};
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    g_free(url);
    
    if (res != CURLE_OK || resp.data->len == 0) {
        g_byte_array_unref(resp.data);
        return NULL;
    }
    
    // Strip docker multiplexing headers (8 bytes per chunk if tty=false) or simply make it valid UTF-8
    // To be safe for GtkTextBuffer, we just make it valid UTF-8. 
    g_byte_array_append(resp.data, (guint8*)"\0", 1);
    
    // Quick heuristic: if the output has binary headers (8 bytes starting with 0x01 or 0x02), we should ideally parse it.
    // For simplicity, we just strip non-printable characters except newlines/tabs.
    gchar *valid_str = g_utf8_make_valid((gchar*)resp.data->data, resp.data->len - 1);
    
    GString *clean = g_string_new("");
    for (int i = 0; valid_str[i] != '\0'; i++) {
        if (g_ascii_isprint(valid_str[i]) || valid_str[i] == '\n' || valid_str[i] == '\r' || valid_str[i] == '\t') {
            g_string_append_c(clean, valid_str[i]);
        }
    }
    g_free(valid_str);
    g_byte_array_unref(resp.data);
    return g_string_free(clean, FALSE);
}

gchar* docker_client_inspect_network(const gchar *net_name) {
    if (!net_name) return NULL;
    
    gchar *url = g_strdup_printf("http://localhost/v1.41/networks/%s", net_name);
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        g_free(url);
        return g_strdup("Failed to initialize curl");
    }
    
    CurlResponse resp = {.data = g_byte_array_new()};
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    gchar *result = NULL;
    
    if (res == CURLE_OK && resp.data->len > 0) {
        g_byte_array_append(resp.data, (guint8*)"\0", 1);
        
        JsonParser *parser = json_parser_new();
        GError *err = NULL;
        if (json_parser_load_from_data(parser, (gchar*)resp.data->data, -1, &err)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonGenerator *gen = json_generator_new();
            json_generator_set_root(gen, root);
            json_generator_set_pretty(gen, TRUE);
            result = json_generator_to_data(gen, NULL);
            g_object_unref(gen);
        } else {
            result = g_strdup_printf("Failed to parse network JSON: %s", err->message);
            g_error_free(err);
        }
        g_object_unref(parser);
    } else {
        result = g_strdup_printf("Failed to inspect network (HTTP %ld, CURL %d)", http_code, res);
    }
    
    g_byte_array_unref(resp.data);
    curl_easy_cleanup(curl);
    g_free(url);
    
    return result;
}
