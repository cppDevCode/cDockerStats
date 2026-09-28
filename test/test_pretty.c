#include <json-glib/json-glib.h>
#include <stdio.h>

int main() {
    g_autoptr(GError) err = NULL;
    JsonParser *parser = json_parser_new();
    json_parser_load_from_data(parser, "{\"a\": 1, \"b\": [2, 3]}", -1, &err);
    JsonNode *root = json_parser_get_root(parser);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);
    gchar *res = json_generator_to_data(gen, NULL);
    printf("Pretty:\n%s\n", res);
    g_free(res);
    g_object_unref(gen);
    g_object_unref(parser);
    return 0;
}
