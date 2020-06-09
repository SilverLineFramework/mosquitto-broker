#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <../cJSON/cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

cJSON *graph;

int network_graph_init() {
    graph = cJSON_CreateArray();
}

int network_graph_add_node(struct mosquitto_db *db, struct mosquitto *context) {
    char *json_buf;

    // log__printf(NULL, MOSQ_LOG_NOTICE, "new action");

    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(data, "id", cJSON_CreateString(ptr->subs[i]->topic));
    cJSON_AddItemToArray(graph, root);

    json_buf = cJSON_Print(graph);
    log__printf(NULL, MOSQ_LOG_NOTICE, "%s", json_buf);

    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 60, NULL);

    return 1;
}
