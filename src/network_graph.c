#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <../cJSON/cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

struct network_graph *graph;

int network_graph_init() {
    graph = mosquitto__malloc(sizeof(struct network_graph));
    graph->length = 0;
    graph->max_length = 1;
    graph->vertexes = mosquitto__malloc(sizeof(struct vertex *));
    graph->vertexes[0] = NULL;
    return 0;
}

static struct vertex *create_vertex(const char *id, vertex_type_t type, const char *parent_id) {
    struct vertex *item = mosquitto__malloc(sizeof(struct vertex));
    strcpy(item->id, id);
    if (type == CLIENT)
        strcpy(item->parent_id, parent_id);
    item->edge_list = NULL;
    item->type = type;
    return item;
}

static struct edge *create_edge(const char *id, edge_type_t type) {
    struct edge *item = mosquitto__malloc(sizeof(struct edge));
    strcpy(item->id, id);
    item->next = NULL;
    item->type = type;
    return item;
}


static struct vertex *find_vertex_with_id(const char *id) {
    struct vertex *elem, *res = NULL;

    for (int i = 0; i < graph->length; ++i) {
        elem = graph->vertexes[i];
        if (elem == NULL) continue;
        if (strcmp(id, elem->id) == 0) {
            res = elem;
            break;
        }
    }
    return res;
}

static struct edge *find_edge(const char *vertex_id, const char *edge_id, edge_type_t type) {
    struct edge *elem, *curr, *res = NULL;
    struct vertex *vertex = find_vertex_with_id(vertex_id);
    curr = vertex->edge_list;

    for(; curr != NULL; curr=curr->next) {
        if (curr->type != type) continue;
        if (strcmp(curr->id, edge_id) == 0) {
            res = elem;
            break;
        }
    }
    return res;
}

static int graph_add_vertex(const char *id, vertex_type_t type, const char *parent_id) {
    struct vertex *item = create_vertex(id, type, parent_id);

    for (int i = 0; i < graph->length; ++i) {
        if (graph->vertexes[i] == NULL) {
            graph->vertexes[i] = item;
            return 0;
        }
    }

    ++graph->length;
    if (graph->length > graph->max_length) {
        graph->vertexes = mosquitto__realloc(graph->vertexes, 2*graph->max_length*sizeof(struct vertex *));
        graph->max_length *= 2;
    }
    graph->vertexes[graph->length-1] = item;

    // for (int i = 0; i < graph->length; ++i) {
    //     if (graph->vertexes[i] != NULL) {
    //         log__printf(NULL, MOSQ_LOG_NOTICE, "%p", graph->vertexes[i]->edge_list);
    //     }
    // }

    return 0;
}

static int graph_add_edge(const char *source, const char *target, edge_type_t type) {
    struct vertex *vertex = find_vertex_with_id(source);
    if (vertex == NULL) return -1;
    struct edge *new = create_edge(target, type);

    if (vertex->edge_list == NULL) {
        vertex->edge_list = new;
        vertex->edge_tail = new;
    }
    else {
        vertex->edge_tail->next = new;
        vertex->edge_tail = new;
    }

    return 0;
}

static int graph_delete_vertex(const char *id) {
    struct vertex *elem, *res = NULL;
    for (int i = 0; i < graph->length; ++i) {
        elem = graph->vertexes[i];
        if (elem == NULL)
            continue;

        if (strcmp(id, elem->id) == 0) {
            graph->vertexes[i] = NULL;
            break;
        }
    }
    return 0;
}
static cJSON *create_container_json(const char *address,
                                double bbox_x, double bbox_y,
                                double bbox_w, double bbox_h,
                                double pos_x, double pos_y) {
    cJSON *root, *data, *bbox, *position;

    /* {
        "data": {
          "id": "glyph0",
          "class": "compartment",
          "label": "synaptic button",
          "clonemarker": false,
          "stateVariables": [],
          "unitsOfInformation": [],
          "bbox": {
            "x": 284.06559650332434,
            "y": 349.4864352956872,
            "w": 528.3772339877597,
            "h": 600.409043170399
          }
        },
        "position": {
          "x": 141.80241206888627,
          "y": 412.2331935302638
        },
        "group": "nodes",
        "removed": false,
        "selected": false,
        "selectable": true,
        "locked": false,
        "grabbable": true,
        "pannable": false,
        "classes": ""
    } */

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
    cJSON_AddItemToObject(bbox, "x", cJSON_CreateNumber(bbox_x));
    cJSON_AddItemToObject(bbox, "y", cJSON_CreateNumber(bbox_y));
    cJSON_AddItemToObject(bbox, "w", cJSON_CreateNumber(bbox_w));
    cJSON_AddItemToObject(bbox, "h", cJSON_CreateNumber(bbox_h));
    cJSON_AddItemToObject(data, "bbox", bbox);

    cJSON_AddItemToObject(position, "x", cJSON_CreateNumber(pos_x));
    cJSON_AddItemToObject(position, "y", cJSON_CreateNumber(pos_y));

    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(root, "position", position);
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("nodes"));
    cJSON_AddItemToObject(root, "removed", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "selected", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "selectable", cJSON_CreateTrue());
    cJSON_AddItemToObject(root, "locked", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "grabbable", cJSON_CreateTrue());
    cJSON_AddItemToObject(root, "pannable", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "classes", cJSON_CreateString(""));

    return root;
}

static cJSON *create_edge_json(const char *node_id1, const char *node_id2) {
    cJSON *root, *data;
    char buf[2*MAX_ID_LEN];

    /*{
        "data": {
          "id": "glyph8-glyph15",
          "class": "necessary stimulation",
          "cardinality": 0,
          "source": "glyph8",
          "target": "glyph15",
          "bendPointPositions": [],
          "portSource": "glyph8",
          "portTarget": "glyph15"
        },
        "group": "edges",
        "removed": false,
        "selected": false,
        "selectable": true,
        "locked": false,
        "grabbable": true,
        "pannable": true,
        "classes": ""
    }*/

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
    cJSON_AddItemToObject(data, "bendPointPositions", cJSON_CreateArray());
    cJSON_AddItemToObject(data, "portSource", cJSON_CreateString(node_id1));
    cJSON_AddItemToObject(data, "portTarget", cJSON_CreateString(node_id2));

    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddItemToObject(root, "group", cJSON_CreateString("edges"));
    cJSON_AddItemToObject(root, "removed", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "selected", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "selectable", cJSON_CreateTrue());
    cJSON_AddItemToObject(root, "locked", cJSON_CreateFalse());
    cJSON_AddItemToObject(root, "grabbable", cJSON_CreateTrue());
    cJSON_AddItemToObject(root, "pannable", cJSON_CreateTrue());
    cJSON_AddItemToObject(root, "classes", cJSON_CreateString(""));

    return root;
}

static cJSON *create_client_json(const char *id, const char *parent_id,
                            double bbox_x, double bbox_y,
                            double bbox_w, double bbox_h,
                            double pos_x, double pos_y) {
    cJSON *client, *data, *data_id, *data_label, *data_class;
    client = create_container_json(id, bbox_x, bbox_y, bbox_w, bbox_h, pos_x, pos_y);

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

static cJSON *create_topic_json(const char *id,
                            double bbox_x, double bbox_y,
                            double bbox_w, double bbox_h,
                            double pos_x, double pos_y) {
    cJSON *topic, *data, *data_id, *data_label, *data_class;
    topic = create_container_json(id, bbox_x, bbox_y, bbox_w, bbox_h, pos_x, pos_y);

    data = cJSON_GetObjectItem(topic, "data");
    data_id = cJSON_GetObjectItem(data, "id");
    data_label = cJSON_GetObjectItem(data, "label");
    data_class = cJSON_GetObjectItem(data, "class");

    cJSON_SetValuestring(data_id, id);
    cJSON_SetValuestring(data_label, id);
    cJSON_SetValuestring(data_class, "simple chemical");

    return topic;
}

int network_graph_delete_node(struct mosquitto *context) {
    graph_delete_vertex(context->id);
    return 0;
}

int network_graph_add_node(struct mosquitto *context) {
    if (find_vertex_with_id(context->id) == NULL) {
        if (find_vertex_with_id(context->address) == NULL) {
            graph_add_vertex(context->address, IP_CONTAINER, NULL);
        }
        graph_add_vertex(context->id, CLIENT, context->address);
    }
    return 0;
}

int network_graph_add_subtopic(struct mosquitto *context) {
    struct mosquitto__subhier *ptr;

    assert(context->sub_count > 0);

    ptr = context->subs[context->sub_count-1]; // latest subbed topic
    if (find_edge(context->id, ptr->topic, CLIENT_TOPIC) == NULL) {
        // log__printf(NULL, MOSQ_LOG_NOTICE, "%s %s", context->id, ptr->topic);
        graph_add_edge(context->id, ptr->topic, CLIENT_TOPIC);
    }

    // traverse through parent topics: hello/test/sub_test ==> hello <- test <- sub_test
    for (; ptr != NULL && strlen(ptr->topic) > 0; ptr = ptr->parent) {
        if (find_vertex_with_id(ptr->topic) == NULL) {
            graph_add_vertex(ptr->topic, TOPIC, NULL);
        }

        if (ptr->parent != NULL && strlen(ptr->parent->topic) > 0) {
            if (find_edge(ptr->topic, ptr->parent->topic, TOPIC_TOPIC) == NULL) {
                graph_add_edge(ptr->topic, ptr->parent->topic, TOPIC_TOPIC);
            }
        }
    }

    return 0;
}

int graph_pub(struct mosquitto_db *db) {
    char *json_buf;
    cJSON *root = cJSON_CreateArray(), *to_add;
    struct edge *curr;

    // log__printf(NULL, MOSQ_LOG_NOTICE, "{");
    for (int i = 0; i < graph->length; ++i) {
        if (graph->vertexes[i] != NULL) {
            if (graph->vertexes[i]->type == IP_CONTAINER) {
                to_add = create_container_json(graph->vertexes[i]->id, 0, 0, 0, 0, 0, 0);
            }
            else if (graph->vertexes[i]->type == CLIENT) {
                to_add = create_client_json(graph->vertexes[i]->id, graph->vertexes[i]->parent_id, 0, 0, 0, 0, 0, 0);
            }
            else if (graph->vertexes[i]->type == TOPIC) {
                to_add = create_topic_json(graph->vertexes[i]->id, 0, 0, 0, 0, 0, 0);
            }

            cJSON_AddItemToArray(root, to_add);
            // log__printf(NULL, MOSQ_LOG_NOTICE, "added %s", graph->vertexes[i]->id);

            curr = graph->vertexes[i]->edge_list;
            for(; curr != NULL; curr=curr->next) {
                to_add = create_edge_json(graph->vertexes[i]->id, curr->id);
                cJSON_AddItemToArray(root, to_add);
                log__printf(NULL, MOSQ_LOG_NOTICE, "added %s-%s", graph->vertexes[i]->id, curr->id);
            }
        }
    }
    // log__printf(NULL, MOSQ_LOG_NOTICE, "}");

    json_buf = cJSON_Print(root);
    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 10, NULL);
    return 0;
}
