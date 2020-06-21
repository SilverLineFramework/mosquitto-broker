#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <../cJSON/cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

#define MAX_TOPIC_LEN   32767
#define BUFLEN          100
#define PUB_DEL_TIMEOUT 20

// Useful constants //
const char s[2] = "/";
const char *ip_class = "client ip";
const char *client_class = "client";
const char *topic_class = "topic";
const char *edge_class = "connections";

/*****************************************************************************/

struct network_graph *graph = NULL;

/*****************************************************************************/

/*
 * General purpose hash function
 */
static unsigned long sdbm_hash(const char *str) {
    unsigned long hash = -1, c;
    while((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

/*
 * Creates a template cJSON struct to be used by all nodes/edges
 */
static cJSON *create_generic_json(const char *id) {
    cJSON *root, *data;

    root = cJSON_CreateObject();
    data = cJSON_CreateObject();

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(id));
    cJSON_AddItemToObject(data, "label", cJSON_CreateString(id));

    cJSON_AddItemToObject(root, "data", data);

    return root;
}

/*
 * Creates a cJSON struct for an IP container
 */
static cJSON *create_ip_json(const char *address) {
    cJSON *root, *data;

    root = create_generic_json(address);
    data = cJSON_GetObjectItem(root, "data");

    cJSON_AddItemToObject(data, "class", cJSON_CreateString(ip_class));
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));

    return root;
}

/*
 * Creates a cJSON struct for an edge
 */
static cJSON *create_edge_json(const char *node1, const char *node2) {
    cJSON *root, *data;
    char buf[MAX_TOPIC_LEN];

    strcpy(buf, node1);
    strcat(buf, "-");
    strcat(buf, node2);

    root = create_generic_json(buf);
    data = cJSON_GetObjectItem(root, "data");

    cJSON_AddItemToObject(data, "source", cJSON_CreateString(node1));
    cJSON_AddItemToObject(data, "target", cJSON_CreateString(node2));

    cJSON_AddItemToObject(data, "class", cJSON_CreateString(edge_class));
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("edges"));

    return root;
}

/*
 * Creates a cJSON struct for a client node
 */
static cJSON *create_client_json(const char *client, const char *parent) {
    cJSON *root, *data;

    root = create_generic_json(client);
    data = cJSON_GetObjectItem(root, "data");

    cJSON_AddItemToObject(data, "class", cJSON_CreateString(client_class));
    cJSON_AddItemToObject(data, "parent", cJSON_CreateString(parent));
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));

    return root;
}

/*
 * Creates a cJSON struct for a topic node
 */
static cJSON *create_topic_json(const char *topic) {
    cJSON *root, *data;

    root = create_generic_json(topic);
    data = cJSON_GetObjectItem(root, "data");

    cJSON_AddItemToObject(data, "class", cJSON_CreateString(topic_class));
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));

    return root;
}

/*
 * Creates an ip container struct from a given IP address
 */
static struct ip_container *create_ip_container(const char *ip) {
    struct ip_container *ip_cont = (struct ip_container *)mosquitto__malloc(sizeof(struct ip_container));
    if (!ip_cont) return NULL;
    ip_cont->json = create_ip_json(ip);
    ip_cont->next = NULL;
    ip_cont->prev = NULL;
    ip_cont->client_list = NULL;
    ip_cont->hash = sdbm_hash(ip);
    return ip_cont;
}

/*
 * Creates an client struct from a given client id and ip address
 */
static struct client *create_client(const char *id, const char *address) {
    struct client *client = (struct client *)mosquitto__malloc(sizeof(struct client));
    if (!client) return NULL;
    client->json = create_client_json(id, address);
    client->pub_json = NULL;
    client->next = NULL;
    client->prev = NULL;
    client->pub_topic = NULL;
    client->hash = sdbm_hash(id);
    client->bytes_out = 0;
    client->total_bytes_out = 0;
    client->bytes_out_per_sec = 0;
    return client;
}

/*
 * Creates an topic struct from a given topic name
 */
static struct topic *create_topic(const char *name) {
    struct topic *topic = (struct topic *)mosquitto__malloc(sizeof(struct topic));
    if (!topic) return NULL;
    topic->json = create_topic_json(name);
    topic->sub_list = NULL;
    topic->next = NULL;
    topic->prev = NULL;
    topic->ref_cnt = 0;
    topic->timeout = 0;
    topic->bytes = 0;
    topic->total_bytes = 0;
    topic->bytes_per_sec = 0;
    topic->hash = sdbm_hash(name);
    topic->full_name = strdup(name);
    return topic;
}

/*
 * Creates an subscriber edge struct source and target.
 * subscribe == client -> topic
 * src should be a topic, tgt should be a client.
 */
static struct sub_edge *create_sub_edge(const char *src, const char *tgt) {
    struct sub_edge *sub_edge = (struct sub_edge *)mosquitto__malloc(sizeof(struct sub_edge));
    if (!sub_edge) return NULL;
    sub_edge->json = create_edge_json(src, tgt);
    sub_edge->sub = NULL;
    sub_edge->next = NULL;
    sub_edge->prev = NULL;
    return sub_edge;
}

/*
 * Searches for an IP container given an address
 */
static struct ip_container *find_ip_container(const char *address) {
    unsigned long hash = sdbm_hash(address);
    struct ip_container *ip_cont = graph->ip_list;
    for (; ip_cont != NULL; ip_cont = ip_cont->next) {
        if (hash == ip_cont->hash) {
            return ip_cont;
        }
    }
    return NULL;
}

/*
 * Adds an IP container to the network graph
 */
static int graph_add_ip_container(struct ip_container *ip_cont) {
    ip_cont->next = graph->ip_list;
    if (graph->ip_list != NULL) {
        graph->ip_list->prev = ip_cont;
    }
    graph->ip_list = ip_cont;
    return 0;
}

/*
 * Searches for a client in an IP container given a client id
 */
static struct client *find_client(struct ip_container *ip_cont, const char *id) {
    unsigned long hash = sdbm_hash(id);
    struct client *curr = ip_cont->client_list;
    for (; curr != NULL; curr = curr->next) {
        if (hash == curr->hash) {
            return curr;
        }
    }
    return NULL;
}

/*
 * Adds a client to an IP container
 */
static int graph_add_client(struct ip_container *ip_cont, struct client *client) {
    client->next = ip_cont->client_list;
    if (ip_cont->client_list != NULL) {
        ip_cont->client_list->prev = client;
    }
    ip_cont->client_list = client;
    return 0;
}

/*
 * Searches for a published topic
 */
static struct topic *find_pub_topic(const char *topic) {
    unsigned long hash = sdbm_hash(topic);
    struct topic *curr = graph->topic_list;
    for (; curr != NULL; curr = curr->next) {
        if (hash == curr->hash) {
            return curr;
        }
    }
    return NULL;
}

/*
 * Searches for a sub edge
 */
static struct sub_edge *find_sub_edge(struct topic *topic, struct client *client) {
    struct sub_edge *curr = topic->sub_list;
    for (; curr != NULL; curr = curr->next) {
        if (client == curr->sub) {
            return curr;
        }
    }
    return NULL;
}

/*
 * Searches for a subscribed topic
 */
static struct topic *find_sub_topic(const char *topic) {
    bool match;
    struct topic *curr = graph->topic_list;
    for (; curr != NULL; curr = curr->next) {
        mosquitto_topic_matches_sub(topic, curr->full_name, &match);
        if (match) {
            return curr;
        }
    }
    return NULL;
}

/*
 * Adds a topic to the topic list
 */
static int graph_add_topic(struct topic *topic) {
    topic->next = graph->topic_list;
    if (graph->topic_list != NULL) {
        graph->topic_list->prev = topic;
    }
    graph->topic_list = topic;
    return 0;
}

/*
 * Deletes all the subcription edges of a topic
 */
static int graph_delete_topic_sub_edges(struct topic *topic) {
    struct sub_edge *curr = topic->sub_list, *temp;
    while (curr != NULL) {
        temp = curr->next;
        mosquitto__free(curr);
        curr = temp;
    }
    topic->sub_list = NULL;
    return 0;
}

/*
 * Detaches a topic from the topic list
 */
static inline void graph_detach_topic(struct topic *topic) {
    if (graph->topic_list == topic) {
        graph->topic_list = graph->topic_list->next;
    }
    if (topic->next != NULL) {
        topic->next->prev = topic->prev;
    }
    if (topic->prev != NULL) {
        topic->prev->next = topic->next;
    }
}

/*
 * Deletes a topic
 */
static int graph_delete_topic(struct topic *topic) {
    graph_detach_topic(topic);
    cJSON_Delete(topic->json);
    mosquitto__free(topic->full_name);
    mosquitto__free(topic);
    return 0;
}

/*
 * Adds a sub edge to the sub list
 */
static int graph_add_sub_edge(struct topic *topic, struct sub_edge *sub_edge) {
    sub_edge->next = topic->sub_list;
    if (topic->sub_list != NULL) {
        topic->sub_list->prev = sub_edge;
    }
    topic->sub_list = sub_edge;
    return 0;
}
/*
 * Deletes a sub edge from the sub list
 */
static int graph_delete_sub_edge(struct topic *topic, struct client *client) {
    struct sub_edge *sub_edge = find_sub_edge(topic, client);
    if (sub_edge == NULL) return -1;
    if (topic->sub_list == sub_edge) {
        topic->sub_list = topic->sub_list->next;
    }
    if (sub_edge->next != NULL) {
        sub_edge->next->prev = sub_edge->prev;
    }
    if (sub_edge->prev != NULL) {
        sub_edge->prev->next = sub_edge->next;
    }
    cJSON_Delete(sub_edge->json);
    mosquitto__free(sub_edge);
    return 0;
}

/*
 * Deletes an IP container
 */
static int graph_delete_ip(struct ip_container *ip_cont) {
    if (graph->ip_list == ip_cont) {
        graph->ip_list = graph->ip_list->next;
    }
    if (ip_cont->next != NULL) {
        ip_cont->next->prev = ip_cont->prev;
    }
    if (ip_cont->prev != NULL) {
        ip_cont->prev->next = ip_cont->next;
    }
    cJSON_Delete(ip_cont->json);
    mosquitto__free(ip_cont);
    return 0;
}

/*
 * Delete a client
 */
static int graph_delete_client(struct ip_container *ip_cont, struct client *client) {
    if (ip_cont->client_list == client) {
        ip_cont->client_list = ip_cont->client_list->next;
    }
    if (client->next != NULL) {
        client->next->prev = client->prev;
    }
    if (client->prev != NULL) {
        client->prev->next = client->next;
    }

    // if IP container is empty, delete it
    if (ip_cont->client_list == NULL) {
        graph_delete_ip(ip_cont);
    }

    // unlink client from all subbed topics
    struct topic *topic = graph->topic_list;
    for (; topic != NULL; topic = topic->next) {
        graph_delete_sub_edge(topic, client);
    }

    // if topic is only one pubbed, delete topic by setting timeout
    if (client->pub_topic != NULL) {
        --client->pub_topic->ref_cnt;
        if (client->pub_topic->ref_cnt == 0) {
            client->pub_topic->timeout = PUB_DEL_TIMEOUT;
            // graph_delete_topic(client->pub_topic);
        }
    }

    cJSON_Delete(client->json);
    cJSON_Delete(client->pub_json);
    mosquitto__free(client);
    return 0;
}

/*****************************************************************************/

int network_graph_init(void) {
    graph = (struct network_graph *)mosquitto__malloc(sizeof(struct network_graph));
    if (!graph) return -1;
    graph->ip_list = NULL;
    graph->topic_list = NULL;
    return 0;
}

/*
 * Called after client connects
 */
int network_graph_add_client(struct mosquitto *context) {
    struct ip_container *ip_cont;
    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        ip_cont = create_ip_container(context->address);
        graph_add_ip_container(ip_cont);
    }

    if (find_client(ip_cont, context->id) == NULL) {
        graph_add_client(ip_cont, create_client(context->id, context->address));
    }

    return 0;
}

/*
 * Called after client publishes to topic
 */
int network_graph_add_pubtopic(struct mosquitto *context, const char *topic, uint32_t payloadlen) {
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;
    unsigned long topic_hash = sdbm_hash(topic);

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
        return -1;
    }

    if ((topic_vert = find_pub_topic(topic)) == NULL) {
        topic_vert = create_topic(topic);
        graph_add_topic(topic_vert);
    }

    topic_vert->bytes += payloadlen;
    topic_vert->total_bytes += (uint64_t)payloadlen;
    client->bytes_out += payloadlen;
    client->total_bytes_out += (uint64_t)payloadlen;

    if (client->pub_topic != NULL) { // client is publishing to a topic already
        if (topic_vert != client->pub_topic) { // client is publishing to a new topic
            // unpublish to old topic
            --client->pub_topic->ref_cnt;
            if (client->pub_topic->ref_cnt == 0) {
                client->pub_topic->timeout = PUB_DEL_TIMEOUT;
                // graph_delete_topic(client->pub_topic);
            }

            // publish to new topic
            ++topic_vert->ref_cnt;
        }
        else {  // client is publishing to the same topic
            return 0;
        }
    }
    else {
        ++topic_vert->ref_cnt;
    }

    cJSON_Delete(client->pub_json);
    client->pub_json = create_edge_json(context->id, topic);
    client->pub_topic = topic_vert;

    return 0;
}

/*
 * Called after client subscribes to topic
 */
int network_graph_add_subtopic(struct mosquitto *context, const char *topic) {
    bool match;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert = graph->topic_list;
    struct sub_edge *sub_edge;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
        return -1;
    }

    for (; topic_vert != NULL; topic_vert = topic_vert->next) {
        mosquitto_topic_matches_sub(topic, topic_vert->full_name, &match);
        if (match) {
            if (find_sub_edge(topic_vert, client) == NULL) {
                sub_edge = create_sub_edge(topic_vert->full_name, context->id);
                graph_add_sub_edge(topic_vert, sub_edge);
                sub_edge->sub = client;
            }
        }
    }

    return 0;
}

/*
 * Called after client unsubscribes to topic
 */
int network_graph_delete_subtopic(struct mosquitto *context, const char *topic) {
    bool match;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert = graph->topic_list;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
        return -1;
    }

    for (; topic_vert != NULL; topic_vert = topic_vert->next) {
        mosquitto_topic_matches_sub(topic, topic_vert->full_name, &match);
        if (match) {
            graph_delete_sub_edge(topic_vert, client);
        }
    }

    return 0;
}

/*
 * Called before client disconnects
 */
int network_graph_delete_client(struct mosquitto *context) {
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
        return -1;
    }
    else {
        if (graph_delete_client(ip_cont, client) < 0) {
            log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not delete client!");
            return -1;
        }
    }

    return 0;
}

/*
 * Unlink all cJSON nodes from a cJSON array
 */
static inline void unlink_json(cJSON *root) {
    cJSON *elem, *temp;
    elem = root->child;
    while (elem != NULL) {
        temp = elem;
        elem = elem->next;
        temp->next = NULL;
    }
}

/*
 * Called every db->config->graph_interval units (currently every 5s)
 */
void network_graph_update(struct mosquitto_db *db, int interval) {
    static time_t last_update = 0;

    char *json_buf, buf[BUFLEN];
    cJSON *root = cJSON_CreateArray(), *data;
    struct ip_container *ip_cont = graph->ip_list;
    struct client *client;
    struct sub_edge *sub_edge;
    struct topic *topic = graph->topic_list, *temp;

    double temp_bytes;
    time_t now = mosquitto_time();

    if (interval && now - interval > last_update) {
        while (topic != NULL) {
            topic->timeout -= (long)(now - last_update); // update timeout
            if (topic->ref_cnt == 0 && topic->timeout <= 0) { // delete topic if timeout is 0
                temp = topic;
                topic = topic->next;
                graph_delete_topic(temp);
            }
            else {
                cJSON_AddItemToArray(root, topic->json);
                sub_edge = topic->sub_list;

                // update incoming bytes/s to topic
                temp_bytes = (double)topic->total_bytes / (now - last_update);
                topic->total_bytes = 0;
                if (fabs(temp_bytes - topic->bytes_per_sec) > 0.01) {
                    topic->bytes_per_sec = temp_bytes;
                    snprintf(buf, BUFLEN, "%.2f bytes/s", topic->bytes_per_sec);
                    for (; sub_edge != NULL; sub_edge = sub_edge->next) {
                        // update incoming bytes/s to client
                        data = cJSON_GetObjectItem(sub_edge->json, "data");
                        cJSON_SetValuestring(cJSON_GetObjectItem(data, "label"), buf);
                        cJSON_AddItemToArray(root, sub_edge->json);
                    }
                }
                else {
                    for (; sub_edge != NULL; sub_edge = sub_edge->next) {
                        cJSON_AddItemToArray(root, sub_edge->json);
                    }
                }
                topic = topic->next;
            }
        }

        // graph has a list of all IP addresses
        for (; ip_cont != NULL; ip_cont = ip_cont->next) {
            cJSON_AddItemToArray(root, ip_cont->json);
            client = ip_cont->client_list; // each IP address holds a list of clients
            for (; client != NULL; client = client->next) {
                cJSON_AddItemToArray(root, client->json);
                if (client->pub_topic != NULL) { // client may have a published topic
                    temp_bytes = (double)client->bytes_out / (now - last_update);
                    client->bytes_out = 0;

                    if (fabs(temp_bytes - client->bytes_out_per_sec) > 0.01) {
                        // update outgoing bytes/s from client
                        client->bytes_out_per_sec = temp_bytes;
                        snprintf(buf, BUFLEN, "%.2f bytes/s", client->bytes_out_per_sec);
                        data = cJSON_GetObjectItem(client->pub_json, "data");
                        cJSON_SetValuestring(cJSON_GetObjectItem(data, "label"), buf);
                    }
                    cJSON_AddItemToArray(root, client->pub_json);
                }
            }
        }

        // send out the updated graph
        json_buf = cJSON_Print(root);
        db__messages_easy_queue(db, NULL, "$SYS/graph", 2, strlen(json_buf), json_buf, 1, 10, NULL);
        unlink_json(root);

        last_update = mosquitto_time();
    }
}

/*
 * Publishes network graph as JSON
 */
int network_graph_pub(struct mosquitto_db *db) {
    char *json_buf;
    cJSON *root = cJSON_CreateArray(), *elem, *temp;
    struct ip_container *ip_cont = graph->ip_list;
    struct client *client;
    struct sub_edge *sub_edge;
    struct topic *topic = graph->topic_list;

    // graph has a list of all IP addresses
    for (; ip_cont != NULL; ip_cont = ip_cont->next) {
        cJSON_AddItemToArray(root, ip_cont->json);
        client = ip_cont->client_list; // each IP address holds a list of clients
        for (; client != NULL; client = client->next) {
            cJSON_AddItemToArray(root, client->json);
            if (client->pub_topic != NULL) { // client may have a published topic
                cJSON_AddItemToArray(root, client->pub_json);
            }
        }
    }

    // graph has a list of all topics that are pubbed
    for (; topic != NULL; topic = topic->next) {
        cJSON_AddItemToArray(root, topic->json);
        sub_edge = topic->sub_list; // each topic has a list of subscribed clients
        for (; sub_edge != NULL; sub_edge = sub_edge->next) {
            cJSON_AddItemToArray(root, sub_edge->json);
        }
    }

    json_buf = cJSON_Print(root);
    // publish to $SYS/graph topic
    db__messages_easy_queue(db, NULL, "$SYS/graph", 2, strlen(json_buf), json_buf, 1, 10, NULL);
    unlink_json(root);

    return 0;
}
