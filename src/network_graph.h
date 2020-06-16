#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <../cJSON/cJSON.h>

#include "mosquitto.h"

struct client;

struct sub_edge {
    cJSON *json;                // edge JSON
    struct client *sub;         // pointer to client that is subbed to topic
    struct sub_edge *next;
};

struct topic {
    cJSON *json;
    struct topic *next;
    struct sub_edge *sub_list;  // list of all subscriptions
    char *full_name;            // full topic name
    unsigned long hash;         // hash(full_name)
    unsigned int ref_cnt;       // # of clients pubbed to topic
    time_t timeout;             // time until deletion
};

struct client {
    cJSON *json;
    cJSON *pub_json;            // JSON of currently pubbed topic
    struct client *next;
    struct topic *pub_topic;    // current topic client is pubbing to
    unsigned long hash;         // hash(client_id)
};

struct ip_container {
    cJSON *json;
    struct ip_container *next;
    struct client *client_list; // list of all clients with specific IP address
    unsigned long hash;         // hash(IP)
};

struct network_graph {
    struct ip_container *ip_list;   // list of all IP containers
    struct topic *topic_list;       // list of all topics
};

int network_graph_init(void);
int network_graph_add_client(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context, const char *topic);
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic);
int network_graph_delete_client(struct mosquitto *context);
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic);
void network_graph_update(struct mosquitto_db *db, int interval);
int network_graph_pub(struct mosquitto_db *db);

#endif
