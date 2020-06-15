#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <../cJSON/cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

#define MAX_TOPIC_LEN   32767

const char s[2] = "/";

const char *ip_class = "client ip";
const char *client_class = "client";
const char *topic_class = "topic";
const char *edge_class = "connections";

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
    ip_cont->json = create_ip_json(ip);
    ip_cont->next = NULL;
    ip_cont->client_list = NULL;
    ip_cont->hash = sdbm_hash(ip);
    return ip_cont;
}

/*
 * Creates an client struct from a given client id and ip address
 */
static struct client *create_client(const char *id, const char *address) {
    struct client *client = (struct client *)mosquitto__malloc(sizeof(struct client));
    client->json = create_client_json(id, address);
    client->pub_json = NULL;
    client->next = NULL;
    client->pub_topic = NULL;
    client->hash = sdbm_hash(id);
    return client;
}

/*
 * Creates an topic struct from a given topic name
 */
static struct topic *create_topic(const char *name) {
    struct topic *topic = (struct topic *)mosquitto__malloc(sizeof(struct topic));
    topic->json = create_topic_json(name);
    topic->sub_list = NULL;
    topic->next = NULL;
    topic->ref_cnt = 0;
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
    sub_edge->json = create_edge_json(src, tgt);
    sub_edge->sub = NULL;
    sub_edge->next = NULL;
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
    graph->ip_list = ip_cont;
    return 0;
}

/*
 * Searches for a client in an IP container given a client id
 */
static struct client *find_client(struct ip_container *ip_cont, const char *id) {
    unsigned long hash = sdbm_hash(id);
    struct client *client = ip_cont->client_list;
    for (; client != NULL; client = client->next) {
        if (hash == client->hash) {
            return client;
        }
    }
    return NULL;
}

/*
 * Adds a client to an IP container
 */
static int graph_add_client(struct ip_container *ip_cont, struct client *client) {
    client->next = ip_cont->client_list;
    ip_cont->client_list = client;
    return 0;
}

/*
 * Searches for a published topic
 */
static struct topic *find_pub_topic(const char *topic_name) {
    struct topic *topic = graph->topic_list;
    for (; topic != NULL; topic = topic->next) {
        if (strcmp(topic_name, topic->full_name) == 0) {
            return topic;
        }
    }
    return NULL;
}


/*
 * Searches for a subscribed topic
 */
static struct topic *find_sub_topic(const char *id) {
    bool result;
    struct topic *topic = graph->topic_list;
    for (; topic != NULL; topic = topic->next) {
        mosquitto_topic_matches_sub(id, topic->full_name, &result);
        if (result) {
            return topic;
        }
    }
    return NULL;
}

/*
 * Adds a topic to the topic list
 */
static int graph_add_topic(struct topic *topic) {
    topic->next = graph->topic_list;
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
 * Deletes a topic
 */
static int graph_delete_topic(struct topic *topic) {
    struct topic *curr = graph->topic_list, *prev = NULL;
    for (; curr != NULL; curr = curr->next) {
        if (curr == topic) {
            if (graph->topic_list == curr) {
                graph->topic_list = graph->topic_list->next;
            }
            else {
                prev->next = curr->next;
            }

            graph_delete_topic_sub_edges(curr);
            // log__printf(NULL, MOSQ_LOG_NOTICE, "should delete %s %p", curr->full_name, graph->topic_list);

            cJSON_Delete(curr->json);
            mosquitto__free(curr->full_name);
            mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

/*
 * Adds a sub edge to the sub list
 */
static int graph_add_sub_edge(struct topic *topic, struct sub_edge *sub_edge) {
    sub_edge->next = topic->sub_list;
    topic->sub_list = sub_edge;
    return 0;
}

/*
 * Deletes a sub edge from the sub list
 */
static int graph_delete_sub_edge(struct topic *topic, struct client *client) {
    struct sub_edge *curr = topic->sub_list, *prev;
    for (; curr != NULL; curr = curr->next) {
        if (curr->sub == client) {
            if (topic->sub_list == curr) {
                topic->sub_list = topic->sub_list->next;
            }
            else {
                prev->next = curr->next;
            }

            cJSON_Delete(curr->json);
            mosquitto__free(curr);

            return 0;
        }
        prev = curr;
    }
    return -1;
}

/*
 * Deletes an IP container
 */
static int graph_delete_ip(struct ip_container *ip_cont) {
    struct ip_container *curr = graph->ip_list, *prev;
    for (; curr != NULL; curr = curr->next) {
        if (curr == ip_cont) {
            if (graph->ip_list == curr) {
                graph->ip_list = graph->ip_list->next;
            }
            else {
                prev->next = curr->next;
            }

            cJSON_Delete(curr->json);
            mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

/*
 * Deletes a client
 */
static int graph_delete_client(struct ip_container *ip_cont, struct client *client) {
    struct client *curr = ip_cont->client_list, *prev;
    struct topic *topic;
    for (; curr != NULL; curr = curr->next) {
        if (curr == client) {
            if (ip_cont->client_list == curr) {
                ip_cont->client_list = ip_cont->client_list->next;
            }
            else {
                prev->next = curr->next;
            }

            if (ip_cont->client_list == NULL) {
                graph_delete_ip(ip_cont);
            }

            topic = graph->topic_list;
            for (; topic != NULL; topic = topic->next) {
                graph_delete_sub_edge(topic, curr);
            }

            if (curr->pub_topic != NULL) {
                curr->pub_topic->ref_cnt--;
                if (curr->pub_topic->ref_cnt == 0) {
                    graph_delete_topic(curr->pub_topic);
                }
            }
            curr->pub_topic = NULL;

            cJSON_Delete(curr->json);
            cJSON_Delete(curr->pub_json);
            mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

/*****************************************************************************/

int network_graph_init(void) {
    graph = (struct network_graph *)mosquitto__malloc(sizeof(struct network_graph));
    graph->ip_list = NULL;
    graph->topic_list = NULL;
    return 0;
}

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

int network_graph_add_pubtopic(struct mosquitto *context, const char *topic) {
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
    }

    if ((topic_vert = find_pub_topic(topic)) == NULL) {
        topic_vert = create_topic(topic);
        graph_add_topic(topic_vert);
        topic_vert->ref_cnt++;
    }

    if (client->pub_topic != NULL && client->pub_topic->ref_cnt == 1 && strcmp(topic, client->pub_topic->full_name) != 0) {
        graph_delete_topic(client->pub_topic);
    }

    cJSON_Delete(client->pub_json);
    client->pub_json = create_edge_json(context->id, topic);
    client->pub_topic = topic_vert;

    return 0;
}

int network_graph_add_subtopic(struct mosquitto *context, const char *topic) {
    bool match;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *curr = graph->topic_list;
    struct sub_edge *sub_edge;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
    }

    // iterate through all topics and add sub edges if they dont exist
    for (; curr != NULL; curr = curr->next) {
        mosquitto_topic_matches_sub(context->id, curr->full_name, &match);
        if (!match) {
            sub_edge = create_sub_edge(curr->full_name, context->id);
            graph_add_sub_edge(curr, sub_edge);
            sub_edge->sub = client;
        }
    }

    return 0;
}

int network_graph_delete_subtopic(struct mosquitto *context, const char *topic) {
    bool match;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *curr = graph->topic_list;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
    }

    // iterate through all topics and delete sub edges that are subbed to client
    for (; curr != NULL; curr = curr->next) {
        mosquitto_topic_matches_sub(context->id, curr->full_name, &match);
        if (match) {
            if (graph_delete_sub_edge(curr, client) < 0) {
                log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: unsub from %s", topic);
            }
        }
    }

    return 0;
}

int network_graph_delete_client(struct mosquitto *context) {
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find ip!");
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not find client!");
    }
    else {
        if (graph_delete_client(ip_cont, client) < 0) {
            log__printf(NULL, MOSQ_LOG_NOTICE, "ERROR: could not delete client!");
        }
    }

    return 0;
}

int network_graph_pub(struct mosquitto_db *db) {
    char *json_buf;
    cJSON *root = cJSON_CreateArray(), *elem, *temp;
    struct ip_container *ip_cont = graph->ip_list;
    struct client *client;
    struct sub_edge *sub_edge;
    struct topic *topic = graph->topic_list;

    for (; ip_cont != NULL; ip_cont = ip_cont->next) {
        cJSON_AddItemToArray(root, ip_cont->json);
        client = ip_cont->client_list;
        for (; client != NULL; client = client->next) {
            cJSON_AddItemToArray(root, client->json);
            if (client->pub_topic != NULL) {
                cJSON_AddItemToArray(root, client->pub_json);
            }
        }
    }

    for (; topic != NULL; topic = topic->next) {
        cJSON_AddItemToArray(root, topic->json);
        sub_edge = topic->sub_list;
        for (; sub_edge != NULL; sub_edge = sub_edge->next) {
            cJSON_AddItemToArray(root, sub_edge->json);
        }
    }

    json_buf = cJSON_Print(root);
    db__messages_easy_queue(db, NULL, "$SYS/graph", 2, strlen(json_buf), json_buf, 1, 10, NULL);

    // unlink all nodes
    elem = root->child;
    while (elem != NULL) {
        temp = elem;
        elem = elem->next;
        temp->next = NULL;
    }

    return 0;
}
