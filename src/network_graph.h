#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include <cJSON/cJSON.h>

#include "mosquitto.h"

struct client;
struct topic;


/**
 * @brief Subscription Edge structure. Points to a client that is subcribed to
 *        a specific topic and holds JSON of an edge.
 */
struct sub_edge
{
    cJSON *json;                        /**< edge JSON */
    struct client *sub;                 /**< pointer to client that is subbed to topic */
    struct sub_edge *next;
    struct sub_edge *prev;
};

/**
 * @brief Publisher Edge structure. Points to a topic that is being published to
 *        by a specific client and holds information about incoming bandwidth.
 */
struct pub_edge
{
    int ttl_cnt;                        /**< "time to live counter", if == 0, delete */
    cJSON *json;                        /**< edge JSON */
    struct topic *pub;                  /**< pointer to topic that is client is pubbed to */
    struct pub_edge *next;
    struct pub_edge *prev;
    uint16_t bytes;
    double bytes_per_sec;               /**< incoming bytes/s to topic */
};

/**
 * @brief Topic Node structure. This contains all the information that is needed for a topic.
 *        Points to a list of subcription edges and holds information about outgoing bandwidth.
 */
struct topic
{
    uint8_t retain;                     /**< whether or not topic is retained for ttl_cnt seconds */
    int ttl_cnt;                        /**< "ttl counter", if == 0, delete */
    char *full_name;                    /**< full topic name */
    cJSON *json;                        /**< topic JSON */
    struct topic *next;
    struct topic *prev;
    struct sub_edge *sub_list;          /**< list of all subscriptions */
    uint16_t ref_cnt;                   /**< # of clients pubbed to topic */
    uint16_t bytes;
    double bytes_per_sec;               /**< outgoing bytes/s from topic */
    unsigned long hash;                 /**< hash(full_name) */
};

/**
 * @brief Client Node structure. This contains all the information that is needed for a client.
 *        Points to a list of publisher edges and holds information about client latency
 *        in nanoseconds.
 */
struct client
{
    bool latency_ready;
    uint16_t latency_cnt;               /**< num times latency has been recved */
    cJSON *json;                        /**< client JSON */
    struct client *next;
    struct client *prev;
    struct pub_edge *pub_list;          /**< current topic client is pubbing to */
    time_t latency;                     /**< response time in ns */
    time_t latency_total;               /**< total response time in ns */
    unsigned long hash;                 /**< hash(client_id) */
};

/**
 * @brief IP Container structure. Holds a set of client node structures that have the
 *        same IP address.
 */
struct ip_container
{
    cJSON *json;
    struct ip_container *next;
    struct ip_container *prev;
    struct client_dict *client_dict;    /**< dict of all clients with specific IP address */
    unsigned long hash;                 /**< hash(IP) */
};

/**
 * @brief IP Dictionary structure. Holds a hash table of ip container structures.
 */
struct ip_dict
{
    size_t used;
    size_t max_size;
    struct ip_container **ip_list;      /**< list of all ip addresses */
};

/**
 * @brief Topic Dictionary structure. Holds a hash table of topic node structures.
 */
struct topic_dict {
    size_t used;
    size_t max_size;
    struct topic **topic_list;          /**< list of all topics */
};

/**
 * @brief Client Dictionary structure. Holds a hash table of client node structures.
 */
struct client_dict
{
    size_t used;
    size_t max_size;
    struct client **client_list;        /**< list of all clients connect with ip */
};

/**
 * @brief Network Graph structure. Holds a set of ip containers and a set of topic node
 *        structures.
 */
struct network_graph
{
    bool changed;                       /**< whether or not the graph needs to be updated */
    struct ip_dict *ip_dict;
    struct topic_dict *topic_dict;
};


/**
 * @brief Function for initalizing the main network graph at broker startup.
 *
 * @param[in]   db          mosquitto database structure that stores config params.
 *
 * @return      status code
 */
int network_graph_init(struct mosquitto_db *db);

/**
 * @brief Function for freeing up memory used by the main network graph at
 *        end of broker program.
 *
 * @return      status code.
 */
int network_graph_cleanup(void);

/**
 * @brief Function for adding a client node to the network graph after CONNECT.
 *
 * @param[in]   context     mosquitto client structure.
 *
 * @return      status code.
 */
int network_graph_add_client(struct mosquitto *context);

/**
 * @brief Function for adding a subscription edge to the network graph after
 *        SUBSCRIBE.
 *
 * @param[in]   context     mosquitto client structure.
 * @param[in]   topic       name of topic being subscribed to.
 *
 * @return      status code.
 */
int network_graph_add_sub_edge(struct mosquitto *context, const char *topic);

/**
 * @brief Function for adding a topic node to the network graph after PUBLISH.
 *
 * @param[in]   context     mosquitto client structure.
 * @param[in]   retain      whether or not the last message should be retained.
 * @param[in]   topic       name of topic being subscribed to.
 * @param[in]   payloadlen  length of payload of publish message.
 *
 * @return      status code.
 */
int network_graph_add_topic(struct mosquitto *context, uint8_t retain,
                            const char *topic, uint32_t payloadlen);

/**
 * @brief Function for deleting a client node from the network graph after DISCONNECT.
 *
 * @param[in]   context     mosquitto client structure.
 *
 * @return      status code.
 */
int network_graph_delete_client(struct mosquitto *context);

/**
 * @brief Function for deleting a subscription edge from the network graph after UNSUBSCRIBE.
 *
 * @param[in]   context     mosquitto client structure.
 * @param[in]   topic       name of topic that is subscribed to.
 *
 * @return      status code.
 */
int network_graph_delete_sub_edge(struct mosquitto *context, const char *topic);

/**
 * @brief Function for collecting client response time/latency. Stores start time in
 *        client structure.
 *
 * @param[in]   context     mosquitto client structure.
 * @param[in]   topic       name of topic that is published to.
 *
 * @return      status code.
 */
int network_graph_latency_start(struct mosquitto *context, const char *topic);

/**
 * @brief Function for collecting client response time/latency. Calculates latency by
 *        subtracting current time from previous time.
 *
 * @param[in]   context     mosquitto client structure.
 *
 * @return      status code.
 */
int network_graph_latency_end(struct mosquitto *context);

/**
 * @brief Function to periodically generate and publish JSON that represents the network
 *        graph. Called every interval seconds.
 *
 * @param[in]   db          mosquitto database structure.
 * @param[in]   interval    interval for periodic network graph publishing.
 */
void network_graph_update(struct mosquitto_db *db, int interval);

#endif
