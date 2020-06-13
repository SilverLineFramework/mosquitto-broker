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

#define MAX_TOPIC_LEN 32767

const char s[2] = "/";

struct network_graph *graph = NULL;


static unsigned long sdbm_hash(const char *str) {
    unsigned long hash = -1, c;
    while((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

static cJSON *create_container_json(const char *address) {
    cJSON *root, *data, *bbox, *position;

    root = cJSON_CreateObject();
    data = cJSON_CreateObject();
    position = cJSON_CreateObject();

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(address));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("client ip"));
    cJSON_AddItemToObject(data, "label", cJSON_CreateString(address));

    cJSON_AddItemToObject(position, "x", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(position, "y", cJSON_CreateNumber(0));

    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(root, "position", position);
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));

    return root;
}

static cJSON *create_edge_json(const char *node_id1, const char *node_id2) {
    cJSON *root, *data;
    char buf[MAX_TOPIC_LEN];

    root = cJSON_CreateObject();
    data = cJSON_CreateObject();

    strcpy(buf, node_id1);
    strcat(buf, "-");
    strcat(buf, node_id2);

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(buf));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("edges"));
    cJSON_AddItemToObject(data, "label", cJSON_CreateString(buf));
    cJSON_AddItemToObject(data, "source", cJSON_CreateString(node_id1));
    cJSON_AddItemToObject(data, "target", cJSON_CreateString(node_id2));

    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("edges"));

    return root;
}

static cJSON *create_client_json(const char *id, const char *parent_id) {
    cJSON *client, *data, *data_id, *data_label, *data_class;
    client = create_container_json(id);

    /* "data": {
            ...
            "parent": ""
            ...
        }
    */
    data = cJSON_GetObjectItem(client, "data");
    data_class = cJSON_GetObjectItem(data, "class");

    cJSON_AddItemToObject(data, "parent", cJSON_CreateString(parent_id));
    cJSON_SetValuestring(data_class, "client");

    return client;
}

static cJSON *create_topic_json(const char *id) {
    cJSON *topic, *data, *data_id, *data_label, *data_class;
    topic = create_container_json(id);

    data = cJSON_GetObjectItem(topic, "data");
    data_id = cJSON_GetObjectItem(data, "id");
    data_label = cJSON_GetObjectItem(data, "label");
    data_class = cJSON_GetObjectItem(data, "class");

    cJSON_SetValuestring(data_id, id);
    cJSON_SetValuestring(data_label, id);
    cJSON_SetValuestring(data_class, "simple chemical");

    return topic;
}

int add_topic(struct topic_vertex *vertex, const char *id) {
    char _id[MAX_TOPIC_LEN], *tmp;
    struct topic_name *to_add;
    vertex->full_topic = strdup(id);
    strcpy(_id, id);
    tmp = strtok(_id, s);

    while(tmp != NULL) {
        to_add = (struct topic_name *)mosquitto__malloc(sizeof(struct topic_name));
        to_add->hash_id = sdbm_hash(tmp);
        to_add->next = NULL;

        if (vertex->topic == NULL) {
            vertex->topic = to_add;
            vertex->topic_tail = to_add;
        }
        else {
            vertex->topic_tail->next = to_add;
            vertex->topic_tail = to_add;
        }
        tmp = strtok(NULL, s);
    }

    return 0;
}

static struct ip_vertex *create_ip_vertex(const char *id) {
    struct ip_vertex *item = (struct ip_vertex *)mosquitto__malloc(sizeof(struct ip_vertex));
    item->hash_id = sdbm_hash(id);
    item->next = NULL;
    item->ref_cnt = 0;
    item->json = create_container_json(id);
    return item;
}

static struct client_vertex *create_client_vertex(const char *id, const char *parent_id) {
    struct client_vertex *item = (struct client_vertex *)mosquitto__malloc(sizeof(struct client_vertex));
    item->hash_id = sdbm_hash(id);
    item->next = NULL;
    item->edge_list = NULL;
    item->edge_tail = NULL;
    item->json = create_client_json(id, parent_id);
    return item;
}

static struct topic_vertex *create_topic_vertex(const char *id) {
    struct topic_vertex *item = (struct topic_vertex *)mosquitto__malloc(sizeof(struct topic_vertex));
    item->hash_id = sdbm_hash(id);
    item->next = NULL;
    item->topic = NULL;
    item->topic_tail = NULL;
    item->ref_cnt = 0;
    item->json = create_topic_json(id);
    add_topic(item, id);
    return item;
}

static struct edge *create_edge(const char *source, const char *target) {
    struct edge *item = mosquitto__malloc(sizeof(struct edge));
    item->hash_id = sdbm_hash(target);
    item->next = NULL;
    item->json = create_edge_json(source, target);
    return item;
}

static struct ip_vertex *find_ip_vertex_with_id(const char *id) {
    struct ip_vertex *curr = graph->ip_start;
    unsigned long hash = sdbm_hash(id);

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct client_vertex *find_client_vertex_with_id(const char *id) {
    struct client_vertex *curr = graph->client_start;
    unsigned long hash = sdbm_hash(id);

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct topic_vertex *find_topic_vertex_with_id(const char *id) {
    struct topic_vertex *curr = graph->topic_start;
    unsigned long hash = sdbm_hash(id);

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct edge *find_edge(const char *source, const char *target) {
    struct edge *curr;
    unsigned long hash_tgt = sdbm_hash(target);
    struct client_vertex *vertex = find_client_vertex_with_id(source);
    curr = vertex->edge_list;

    for(; curr != NULL; curr=curr->next) {
        if (hash_tgt == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct client_vertex *graph_add_client_vertex(const char *id, const char *parent_id) {
    struct client_vertex *to_add = create_client_vertex(id, parent_id);

    if (graph->client_start == NULL && graph->client_end == NULL) {
        graph->client_start = to_add;
        graph->client_end = to_add;
    }
    else {
        graph->client_end->next = to_add;
        graph->client_end = to_add;
    }

    return to_add;
}

static struct topic_vertex *graph_add_topic_vertex(const char *id) {
    struct topic_vertex *to_add = create_topic_vertex(id);

    if (graph->topic_start == NULL && graph->topic_end == NULL) {
        graph->topic_start = to_add;
        graph->topic_end = to_add;
    }
    else {
        graph->topic_end->next = to_add;
        graph->topic_end = to_add;
    }

    return to_add;
}

static struct edge *graph_add_edge_at_vert(struct client_vertex *vertex, const char *source, const char *target) {
    struct edge *to_add = create_edge(source, target);

    if (vertex->edge_list == NULL && vertex->edge_tail == NULL) {
        vertex->edge_list = to_add;
        vertex->edge_tail = to_add;
    }
    else {
        vertex->edge_tail->next = to_add;
        vertex->edge_tail = to_add;
    }

    return to_add;
}

static struct edge *graph_add_edge(const char *source, const char *target) {
    struct client_vertex *vertex = find_client_vertex_with_id(source);
    if (vertex == NULL) return NULL;
    struct edge *to_add = graph_add_edge_at_vert(vertex, source, target);
    return to_add;
}

static int graph_add_edges_to_topic(struct mosquitto *context, const char *topic) {
    if (topic[0] == '$') {
        return 0;
    }

    char _topic[MAX_TOPIC_LEN], *tmp;
    struct topic_vertex *curr = graph->topic_start;
    struct topic_name *curr_topic;

    for (; curr != NULL; curr=curr->next) {
        if (curr != NULL) {
            curr_topic = curr->topic;
            if (strcmp(curr->full_topic, topic) == 0) {
                // log__printf(NULL, MOSQ_LOG_NOTICE, "exact match: %s %s", curr->full_topic, topic);
                graph_add_edge(context->id, curr->full_topic);
                continue;
            }

            strcpy(_topic, topic);
            tmp = strtok(_topic, s);
            while(tmp != NULL && curr_topic != NULL) {
                if (tmp[0] == '#') {
                    // log__printf(NULL, MOSQ_LOG_NOTICE, "wildcard match: %s %s", topic, curr->full_topic);
                    graph_add_edge(context->id, curr->full_topic);
                    break;
                }
                else if (sdbm_hash(tmp) == curr_topic->hash_id || tmp[0] == '+') {
                    tmp = strtok(NULL, s);
                    curr_topic = curr_topic->next;
                }
                else { // not equal
                    break;
                }
            }
        }
    }

    return 0;
}

static int graph_delete_ip_vertex_with_hash(unsigned long hash) {
    struct ip_vertex *curr = graph->ip_start, *prev = NULL;

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            if (curr->ref_cnt != 0) return -1;
            if (graph->ip_start == curr && graph->ip_end == curr) {
                graph->ip_start = NULL;
                graph->ip_end = NULL;
            }
            else if (graph->ip_start == curr) {
                graph->ip_start = graph->ip_start->next;
            }
            else if (graph->ip_end == curr) {
                graph->ip_end = prev;
                graph->ip_end->next = NULL;
            }
            else {
                prev->next = curr->next;
            }
            // cJSON_Delete(curr->json);
            // mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

static int graph_delete_topic_vertex_with_hash(unsigned long hash) {
    struct topic_vertex *curr = graph->topic_start, *prev = curr;
    struct topic_name *curr_topic;

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            if (curr->ref_cnt == 1) {
                if (graph->topic_start == curr && graph->topic_end == curr) {
                    graph->topic_start = NULL;
                    graph->topic_end = NULL;
                }
                else if (graph->topic_start == curr) {
                    graph->topic_start = graph->topic_start->next;
                }
                else if (graph->topic_end == curr) {
                    graph->topic_end = prev;
                    graph->topic_end->next = NULL;
                }
                else {
                    prev->next = curr->next;
                }

                struct topic_name *temp;
                while (curr->topic != NULL) {
                    temp = curr->topic;
                    curr->topic = curr->topic->next;
                    mosquitto__free(temp);
                }
                mosquitto__free(curr->full_topic);
                // cJSON_Delete(curr->json);
                // mosquitto__free(curr);
                return 0;
            }
            else {
                curr->ref_cnt--;
                return 1;
            }
        }
        prev = curr;
    }
    return -1;
}

static int graph_delete_client_vertex_with_hash(unsigned long hash) {
    struct client_vertex *curr = graph->client_start, *prev = curr;
    struct topic_name *curr_topic;

    for (; curr != NULL; curr=curr->next) {
        if (hash == curr->hash_id) {
            curr->parent->ref_cnt--;
            if (curr->parent->ref_cnt == 0)
                graph_delete_ip_vertex_with_hash(curr->parent->hash_id);

            if (graph->client_start == curr && graph->client_end == curr) {
                graph->client_start = NULL;
                graph->client_end = NULL;
            }
            else if (graph->client_start == curr) {
                graph->client_start = graph->client_start->next;
            }
            else if (graph->client_end == curr) {
                graph->client_end = prev;
                graph->client_end->next = NULL;
            }
            else {
                prev->next = curr->next;
            }

            struct edge *temp;
            while (curr->edge_list != NULL) {
                temp = curr->edge_list;
                curr->edge_list = curr->edge_list->next;
                if (graph_delete_topic_vertex_with_hash(temp->hash_id) < 0) {
                    log__printf(NULL, MOSQ_LOG_NOTICE, "error deleting topic vertex");
                }
                mosquitto__free(temp);
            }
            // cJSON_Delete(curr->json);
            // mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

static int graph_delete_edge(const char *source, const char *target) {
    struct client_vertex *vertex = find_client_vertex_with_id(source);
    unsigned long hash_tgt = sdbm_hash(target);
    struct edge *curr = vertex->edge_list, *prev = curr;

    for (; curr != NULL; curr=curr->next) {
        if (hash_tgt == curr->hash_id) {
            if (vertex->edge_list == curr && vertex->edge_tail == curr) {
                vertex->edge_list = NULL;
                vertex->edge_tail = NULL;
            }
            else if (vertex->edge_list == curr) {
                vertex->edge_list = vertex->edge_list->next;
            }
            else if (vertex->edge_tail == curr) {
                vertex->edge_tail = prev;
                vertex->edge_tail->next = NULL;
            }
            else {
                prev->next = curr->next;
            }
            // cJSON_Delete(curr->json);
            // mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

int network_graph_init() {
    graph = (struct network_graph *)mosquitto__malloc(sizeof(struct network_graph));
    graph->ip_start = NULL;
    graph->ip_end = NULL;
    graph->client_start = NULL;
    graph->client_end = NULL;
    graph->topic_start = NULL;
    graph->topic_end = NULL;
    return 0;
}

int network_graph_add_client(struct mosquitto *context) {
    struct client_vertex *vert;
    struct ip_vertex *ip;

    if ((vert = find_client_vertex_with_id(context->id)) == NULL) {
        vert = graph_add_client_vertex(context->id, context->address);

        if ((ip = find_ip_vertex_with_id(context->address)) == NULL) {
            ip = create_ip_vertex(context->address);

            if (graph->ip_start == NULL) {
                graph->ip_start = ip;
                graph->ip_end = ip;
            }
            else {
                graph->ip_end->next = ip;
                graph->ip_end = ip;
            }
        }
        vert->parent = ip;
        vert->parent->ref_cnt++;
        return 0;
    }

    return -1;
}

int network_graph_delete_client(struct mosquitto *context) {
    if (graph_delete_client_vertex_with_hash(sdbm_hash(context->id)) != 0) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "error deleting client vertex");
    }
    // struct topic_vertex *curr = graph->topic_start;
    // for (; curr != NULL; curr=curr->next) {
    //     log__printf(NULL, MOSQ_LOG_NOTICE, "%p", curr);
    // }
    return 0;
}

int network_graph_add_subtopic(struct mosquitto *context, const char *topic) {
    if (graph_add_edges_to_topic(context, topic) != 0) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "error adding edges to topic");
    }
    return 0;
}

int network_graph_add_pubtopic(struct mosquitto *context, const char *topic) {
    struct topic_vertex *top;
    if ((top = find_topic_vertex_with_id(topic)) == NULL) {
        top = graph_add_topic_vertex(topic);
    }
    top->ref_cnt++;

    if (find_edge(context->id, topic) == NULL) {
        graph_add_edge(context->id, topic);
    }

    return 0;
}

int network_graph_delete_subtopic(struct mosquitto *context, const char *topic) {
    if (graph_delete_edge(context->id, topic) != 0) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "unsub failed");
    }
    // log__printf(NULL, MOSQ_LOG_NOTICE, "unsub %s %s", context->id, topic);
    return 0;
}

int network_graph_pub(struct mosquitto_db *db) {
    char *json_buf;
    cJSON *root = cJSON_CreateArray();
    struct ip_vertex *ip = graph->ip_start;
    struct client_vertex *vert = graph->client_start;
    struct topic_vertex *top = graph->topic_start;
    struct edge *edge;

    for (; ip != NULL; ip=ip->next) {
        cJSON_AddItemToArray(root, ip->json);
    }

    for (; vert != NULL; vert=vert->next) {
        cJSON_AddItemToArray(root, vert->json);

        edge = vert->edge_list;
        for(; edge != NULL; edge=edge->next) {
            cJSON_AddItemToArray(root, edge->json);
        }
    }

    top = graph->topic_start;
    for (; top != NULL; top=top->next) {
        cJSON_AddItemToArray(root, top->json);
    }
    log__printf(NULL, MOSQ_LOG_NOTICE, "here: %p", graph->topic_start);

    json_buf = cJSON_Print(root);
    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 10, NULL);
    return 0;
}
