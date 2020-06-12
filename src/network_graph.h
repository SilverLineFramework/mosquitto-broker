#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <../cJSON/cJSON.h>

#include "mosquitto.h"

#define MAX_TOPIC_LEN 32767

typedef enum {
    CLIENT_TOPIC, TOPIC_TOPIC
} edge_type_t;

typedef enum {
    IP_CONTAINER, CLIENT, TOPIC
} vertex_type_t;

struct topic_name {
    struct topic_name *next;
    unsigned long hash_id;
};

struct edge {
    edge_type_t type;
    cJSON *json;
    struct edge *next;
    unsigned long hash_id;
};

struct vertex {
    vertex_type_t type;
    cJSON *json;
    struct edge *edge_list;
    struct edge *edge_tail;
    struct topic_name *topic;
    struct topic_name *topic_tail;
    unsigned long hash_id;
    char full_topic[MAX_TOPIC_LEN];
};

struct network_graph {
    unsigned long length;
    unsigned long max_length;
    struct vertex **vertices;
};

int network_graph_init();
int network_graph_add_client(struct mosquitto *context);
int network_graph_delete_client(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context, const char *topic);
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic);
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic);
int network_graph_pub(struct mosquitto_db *db);

#endif
