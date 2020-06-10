#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

// #include <../cJSON/cJSON.h>

#include "mosquitto.h"

#define MAX_ID_LEN 512

typedef enum {
    CLIENT_TOPIC, TOPIC_TOPIC
} edge_type_t;

typedef enum {
    IP_CONTAINER, CLIENT, TOPIC
} vertex_type_t;

struct edge {
    edge_type_t type;
    struct edge *next;
    char id[MAX_ID_LEN];
};

struct vertex {
    vertex_type_t type;
    struct edge *edge_list;
    struct edge *edge_tail;
    char id[MAX_ID_LEN];
    char parent_id[MAX_ID_LEN];
};

struct network_graph {
    int length;
    int max_length;
    struct vertex **vertexes;
};

int network_graph_init();
int graph_pub(struct mosquitto_db *db);
int network_graph_add_node(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context);
int network_graph_delete_node(struct mosquitto *context);

#endif
