#include <stdio.h>
#include <glib.h>
#include <curl/curl.h>

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    g_string_append_len((GString *)userp, contents, size * nmemb);
    return size * nmemb;
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost/v1.41/networks/fixer_default");
    
    GString *str = g_string_new(NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
    
    curl_easy_perform(curl);
    printf("Output: %s\n", str->str);
    
    g_string_free(str, TRUE);
    curl_easy_cleanup(curl);
    return 0;
}
