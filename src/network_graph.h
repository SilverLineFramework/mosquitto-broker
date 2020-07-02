#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <../cJSON/cJSON.h>

#include "mosquitto.h"

struct client;
struct topic;

struct sub_edge {
    cJSON *json;                // edge JSON
    struct client *sub;         // pointer to client that is subbed to topic
    struct sub_edge *next;
    struct sub_edge *prev;
};

struct pub_edge {
    cJSON *json;                // edge JSON
    struct topic *pub;          // pointer to topic that is client is pubbed to
    struct pub_edge *next;
    struct pub_edge *prev;
    uint32_t bytes_out;
    uint64_t total_bytes_out;
    double bytes_out_per_sec;
    char til_delete;
};

struct topic {
    cJSON *json;
    struct topic *next;
    struct topic *prev;
    struct sub_edge *sub_list;  // list of all subscriptions
    char *full_name;            // full topic name
    unsigned long hash;         // hash(full_name)
    unsigned int ref_cnt;       // # of clients pubbed to topic
    uint32_t bytes;
    uint64_t total_bytes;
    double bytes_per_sec;
    char til_delete;
};

struct client {
    bool latency_ready;
    cJSON *json;
    struct client *next;
    struct client *prev;
    struct pub_edge *pub_list;  // current topic client is pubbing to
    time_t latency;
    unsigned long hash;         // hash(client_id)
};

struct ip_container {
    cJSON *json;
    struct ip_container *next;
    struct ip_container *prev;
    struct client *client_list; // list of all clients with specific IP address
    unsigned long hash;         // hash(IP)
};

struct ip_dict {
    size_t used;
    size_t max_size;
    struct ip_container **ip_list;  // list of all ip addresses
};

struct topic_dict {
    size_t used;
    size_t max_size;
    struct topic **topic_list;      // list of all topics
};

struct network_graph {
    cJSON *json;
    char til_delete;
    struct ip_dict *ip_dict;
    struct topic_dict *topic_dict;
};

int network_graph_init(struct mosquitto_db *db);
int network_graph_cleanup(void);
int network_graph_add_client(struct mosquitto *context);
int network_graph_add_subtopic(struct mosquitto *context, const char *topic);
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic, uint32_t payloadlen);
int network_graph_delete_client(struct mosquitto *context);
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic);
int network_graph_latency_start(struct mosquitto *context);
int network_graph_latency_end(struct mosquitto *context);
void network_graph_update(struct mosquitto_db *db, int interval);
void network_graph_pub(struct mosquitto_db *db);

#endif
