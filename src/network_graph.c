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


struct network_graph *graph;


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
    bbox = cJSON_CreateObject();
    position = cJSON_CreateObject();

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(address));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("compartment"));
    cJSON_AddItemToObject(data, "label", cJSON_CreateString(address));
    cJSON_AddItemToObject(data, "clonemarker", cJSON_CreateFalse());
    cJSON_AddItemToObject(data, "stateVariables", cJSON_CreateArray());
    cJSON_AddItemToObject(data, "unitsOfInformation", cJSON_CreateArray());
    cJSON_AddItemToObject(bbox, "x", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(bbox, "y", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(bbox, "w", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(bbox, "h", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(data, "bbox", bbox);

    cJSON_AddItemToObject(position, "x", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(position, "y", cJSON_CreateNumber(0));

    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(root, "position", position);
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));

    return root;
}

static cJSON *create_edge_json(const char *node_id1, const char *node_id2, edge_type_t type) {
    cJSON *root, *data;
    char buf[MAX_TOPIC_LEN];

    root = cJSON_CreateObject();
    data = cJSON_CreateObject();

    strcpy(buf, node_id1);
    strcat(buf, "-");
    strcat(buf, node_id2);

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(buf));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("necessary stimulation"));
    cJSON_AddItemToObject(data, "cardinality", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(data, "source", cJSON_CreateString(node_id1));
    cJSON_AddItemToObject(data, "target", cJSON_CreateString(node_id2));
    cJSON_AddItemToObject(data, "portSource", cJSON_CreateString(node_id1));
    cJSON_AddItemToObject(data, "portTarget", cJSON_CreateString(node_id2));

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
    cJSON_SetValuestring(data_class, "macromolecule");

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

int add_topic(struct vertex *vertex, const char *id) {
    char _id[MAX_TOPIC_LEN], *tmp;
    struct topic_name *to_add;
    strcpy(vertex->full_topic, id);
    strcpy(_id, id);
    tmp = strtok(_id, "/");

    while(tmp != NULL) {
        to_add = mosquitto__malloc(sizeof(struct topic_name));
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
        tmp = strtok(NULL, "/");
    }

    return 0;
}

static struct vertex *create_vertex(const char *id, vertex_type_t type, const char *parent_id) {
    struct vertex *item = mosquitto__malloc(sizeof(struct vertex));
    item->type = type;
    item->hash_id = sdbm_hash(id);
    item->edge_list = NULL;
    item->edge_tail = NULL;
    item->topic = NULL;
    item->topic_tail = NULL;

    if (type == IP_CONTAINER) {
        item->json = create_container_json(id);
    }
    else if (type == CLIENT) {
        item->json = create_client_json(id, parent_id);
    }
    else if (type == TOPIC) {
        item->json = create_topic_json(id);
        add_topic(item, id);
    }
    return item;
}

static struct edge *create_edge(const char *source, const char *target, edge_type_t type) {
    struct edge *item = mosquitto__malloc(sizeof(struct edge));
    item->hash_id = sdbm_hash(source);
    item->next = NULL;
    item->json = create_edge_json(source, target, type);
    item->type = type;
    return item;
}

static struct vertex *find_vertex_with_id(const char *id) {
    struct vertex *curr;
    unsigned long hash = sdbm_hash(id);

    for (int i = 0; i < graph->length; ++i) {
        curr = graph->vertices[i];
        if (curr != NULL && hash == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct edge *find_edge(const char *source, const char *target, edge_type_t type) {
    struct edge *curr;
    unsigned long hash_tgt = sdbm_hash(target);
    struct vertex *vertex = find_vertex_with_id(source);
    curr = vertex->edge_list;

    for(; curr != NULL; curr=curr->next) {
        if (curr->type != type) continue;
        if (hash_tgt == curr->hash_id) {
            return curr;
        }
    }
    return NULL;
}

static struct vertex *graph_add_vertex(const char *id, vertex_type_t type, const char *parent_id) {
    struct vertex *item = create_vertex(id, type, parent_id);

    for (int i = 0; i < graph->length; ++i) {
        if (graph->vertices[i] == NULL) {
            graph->vertices[i] = item;
            return item;
        }
    }

    ++graph->length;
    if (graph->length > graph->max_length) {
        graph->vertices = mosquitto__realloc(graph->vertices, 2*graph->max_length*sizeof(struct vertex *));
        graph->max_length *= 2;
    }
    graph->vertices[graph->length-1] = item;

    return item;
}

static struct edge *graph_add_edge_at_vert(struct vertex *vertex, const char *source, const char *target, edge_type_t type) {
    struct edge *to_add = create_edge(source, target, type);

    if (vertex->edge_list == NULL) {
        vertex->edge_list = to_add;
        vertex->edge_tail = to_add;
    }
    else {
        vertex->edge_tail->next = to_add;
        vertex->edge_tail = to_add;
    }

    return to_add;
}

static struct edge *graph_add_edge(const char *source, const char *target, edge_type_t type) {
    struct vertex *vertex = find_vertex_with_id(source);
    if (vertex == NULL) return NULL;

    struct edge *to_add = graph_add_edge_at_vert(vertex, source, target, type);
    return to_add;
}

static int graph_add_edges_to_topic(struct mosquitto *context, const char *topic) {
    if (topic[0] == '$') {
        return 0;
    }

    const char s[2] = "/";
    char _topic[MAX_TOPIC_LEN], *tmp;
    struct vertex *curr;
    struct topic_name *curr_topic;

    for (int i = 0; i < graph->length; ++i) {
        curr = graph->vertices[i];
        if (curr != NULL && curr->type == TOPIC) {
            curr_topic = curr->topic;
            if (strcmp(curr->full_topic, topic) == 0) {
                log__printf(NULL, MOSQ_LOG_NOTICE, "exact match: %s %s", curr->full_topic, topic);
                graph_add_edge_at_vert(curr, context->id, curr->full_topic, CLIENT_TOPIC);
                continue;
            }

            strcpy(_topic, topic);
            tmp = strtok(_topic, s);
            while(tmp != NULL && curr_topic != NULL) {
                if (tmp[0] == '#') {
                    log__printf(NULL, MOSQ_LOG_NOTICE, "wildcard match: %s %s", topic, curr->full_topic);
                    graph_add_edge_at_vert(curr, context->id, curr->full_topic, CLIENT_TOPIC);
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
            // log__printf(NULL, MOSQ_LOG_NOTICE, "no match %s %s", topic, curr->full_topic);
        }
    }

    return -1;
}

static int graph_delete_vertex_with_hash(unsigned long hash) {
    struct vertex *elem;

    for (int i = 0; i < graph->length; ++i) {
        elem = graph->vertices[i];
        if (elem != NULL && hash == elem->hash_id) {
            struct edge *curr = elem->edge_list, *temp;
            while (curr != NULL) {
                temp = curr;
                curr = curr->next;
                mosquitto__free(temp);
            }
            mosquitto__free(elem);
            graph->vertices[i] = NULL;
            return 0;
        }
    }
    return -1;
}

static int graph_delete_vertex(const char *id) {
    return graph_delete_vertex_with_hash(sdbm_hash(id));
}

static int graph_delete_edge(const char *source, const char *target) {
    struct vertex *vertex = find_vertex_with_id(source);
    unsigned long hash_tgt = sdbm_hash(target);
    struct edge *curr = vertex->edge_list, *prev = curr;

    for (; curr != NULL; curr=curr->next) {
        if (hash_tgt == curr->hash_id) {
            prev->next = curr->next;
            cJSON_Delete(curr->json);
            mosquitto__free(curr);
            return 0;
        }
        prev = curr;
    }
    return -1;
}

int network_graph_init() {
    graph = mosquitto__malloc(sizeof(struct network_graph));
    graph->length = 0;
    graph->max_length = 1;
    graph->vertices = mosquitto__malloc(sizeof(struct vertex *));
    graph->vertices[0] = NULL;
    return 0;
}

int network_graph_add_client(struct mosquitto *context) {
    if (find_vertex_with_id(context->id) == NULL) {
        if (find_vertex_with_id(context->address) == NULL) {
            graph_add_vertex(context->address, IP_CONTAINER, NULL);
        }
        graph_add_vertex(context->id, CLIENT, context->address);
        return 0;
    }
    return -1;
}

int network_graph_delete_client(struct mosquitto *context) {
    if (graph_delete_vertex(context->id) != 0) {
        log__printf(NULL, MOSQ_LOG_NOTICE, "Error deleting vertex");
    }
    for (int i = 0; i < graph->length; ++i) {
        if (graph->vertices[i] != NULL &&
            graph->vertices[i]->type == TOPIC) {
            graph_delete_vertex_with_hash(graph->vertices[i]->hash_id);
        }
    }
    return 0;
}

int network_graph_add_subtopic(struct mosquitto *context, const char *topic) {
    graph_add_edges_to_topic(context, topic);
    return 0;
}

int network_graph_add_pubtopic(struct mosquitto *context, const char *topic) {
    if (find_vertex_with_id(topic) == NULL) {
        graph_add_vertex(topic, TOPIC, NULL);
    }

    if (find_edge(context->id, topic, CLIENT_TOPIC) == NULL) {
        graph_add_edge(context->id, topic, CLIENT_TOPIC);
    }

    return 0;
}

int network_graph_delete_subtopic(struct mosquitto *context, const char *topic) {
    graph_delete_edge(context->id, topic);
    log__printf(NULL, MOSQ_LOG_NOTICE, "unsub %s %s", context->id, topic);
    return 0;
}

int network_graph_pub(struct mosquitto_db *db) {
    char *json_buf;
    cJSON *root = cJSON_CreateArray();
    struct edge *curr;

    for (int i = 0; i < graph->length; ++i) {
        if (graph->vertices[i] != NULL) {
            cJSON_AddItemToArray(root, graph->vertices[i]->json);

            curr = graph->vertices[i]->edge_list;
            for(; curr != NULL; curr=curr->next) {
                cJSON_AddItemToArray(root, curr->json);
            }
        }
    }

    json_buf = cJSON_Print(root);
    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 10, NULL);
    return 0;
}
