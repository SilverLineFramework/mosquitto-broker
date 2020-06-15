#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <../cJSON/cJSON.h>

#include "mosquitto.h"

struct client;

struct sub_edge {
    cJSON *json;
    struct client *sub;
    struct sub_edge *next;
    // unsigned long client_hash;
};

struct topic {
    cJSON *json;
    struct topic *next;
    struct sub_edge *sub_list;
    char *full_name;
    unsigned int ref_cnt;
};

struct client {
    cJSON *json;
    cJSON *pub_json;
    struct client *next;
    struct topic *pub_topic;
    unsigned long hash;
};

struct ip_container {
    cJSON *json;
    struct ip_container *next;
    struct client *client_list;
    unsigned long hash;
};

struct network_graph {
    struct ip_container *ip_list;
    struct topic *topic_list;
};

int network_graph_init();
int network_graph_add_client(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context, const char *topic);
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic);
int network_graph_delete_client(struct mosquitto *context);
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic);
int network_graph_pub(struct mosquitto_db *db);

#endif
