#include <stdio.h>
#include <glib.h>
#include "core/docker_client.h"

void test_docker_list_containers() {
    GError *err = NULL;
    GList *containers = docker_client_list_containers(&err);
    if (err) {
        printf("Test failed: Error getting containers - %s\n", err->message);
        g_error_free(err);
        return;
    }
    printf("Successfully retrieved container list. Count: %d\n", g_list_length(containers));
    docker_client_free_container_list(containers);
}

int main() {
    printf("Running Core Tests...\n");
    test_docker_list_containers();
    printf("Tests finished.\n");
    return 0;
}
