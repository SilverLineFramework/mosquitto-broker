#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <../cJSON/cJSON.h>

#include "mosquitto.h"

typedef enum {
    PUB, SUB
} edge_type_t;

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

struct ip_vertex {
    cJSON *json;
    struct ip_vertex *next;
    unsigned long hash_id;
    unsigned long ref_cnt;
};

struct client_vertex {
    cJSON *json;
    struct client_vertex *next;
    struct edge *edge_list;
    struct edge *edge_tail;
    struct ip_vertex *parent;
    unsigned long hash_id;
};

struct topic_vertex {
    cJSON *json;
    struct topic_vertex *next;
    char *full_topic;
    struct topic_name *topic;
    struct topic_name *topic_tail;
    unsigned long hash_id;
    unsigned long ref_cnt;
};

struct network_graph {
    struct ip_vertex *ip_start;
    struct ip_vertex *ip_end;
    struct client_vertex *client_start;
    struct client_vertex *client_end;
    struct topic_vertex *topic_start;
    struct topic_vertex *topic_end;
};

int network_graph_init();
int network_graph_add_client(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context, const char *topic);
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic);
int network_graph_delete_client(struct mosquitto *context);
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic);
int network_graph_pub(struct mosquitto_db *db);

#endif
