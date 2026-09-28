#include <stdio.h>
#include <curl/curl.h>

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, "/var/run/docker.sock");
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost/v1.41/networks");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return 0;
}
