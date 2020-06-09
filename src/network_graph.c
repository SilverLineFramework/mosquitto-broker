#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <../cJSON/cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

cJSON *graph;

int network_graph_init() {
    graph = cJSON_CreateArray();
    return 0;
}

static cJSON *find_node(const char *id) {
    cJSON *res = NULL;

    for (int i = 0; i < cJSON_GetArraySize(graph); i++) {
        cJSON *elem, *data, *data_id;
        elem = cJSON_GetArrayItem(graph, i);
        data = cJSON_GetObjectItem(elem, "data");
        data_id = cJSON_GetObjectItem(data, "id");

        if (strcmp(id, data_id->valuestring) == 0) {
            res = elem;
            break;
        }
    }
    return res;
}

static cJSON *find_edge(const char *node1, const char *node2) {
    cJSON *res = NULL;

    for (int i = 0; i < cJSON_GetArraySize(graph); i++) {
        cJSON *elem, *data, *data_id;
        char buf[255];
        elem = cJSON_GetArrayItem(graph, i);
        data = cJSON_GetObjectItem(elem, "data");
        data_id = cJSON_GetObjectItem(data, "id");

        strcpy(buf, node1);
        strcat(buf, "-");
        strcat(buf, node2);

        if (strcmp(buf, data_id->valuestring) == 0) {
            res = elem;
            break;
        }
    }
    return res;
}

static cJSON *create_container(struct mosquitto *context,
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

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(context->address));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("compartment"));
    cJSON_AddItemToObject(data, "label", cJSON_CreateString(context->address));
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

static cJSON *create_edge(struct mosquitto *context,
                          const char *node1, const char *node2) {
    cJSON *root, *data;
    char buf[255];

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

    strcpy(buf, node1);
    strcat(buf, "-");
    strcat(buf, node2);

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(buf));
    cJSON_AddItemToObject(data, "class", cJSON_CreateString("necessary stimulation"));
    cJSON_AddItemToObject(data, "cardinality", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(data, "source", cJSON_CreateString(node1));
    cJSON_AddItemToObject(data, "target", cJSON_CreateString(node2));
    cJSON_AddItemToObject(data, "bendPointPositions", cJSON_CreateArray());
    cJSON_AddItemToObject(data, "portSource", cJSON_CreateString(node1));
    cJSON_AddItemToObject(data, "portTarget", cJSON_CreateString(node2));

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

static cJSON *create_client(struct mosquitto *context, const char *parent_id,
                            double bbox_x, double bbox_y,
                            double bbox_w, double bbox_h,
                            double pos_x, double pos_y) {
    cJSON *client, *data, *data_id, *data_label, *data_class;
    client = create_container(context, bbox_x, bbox_y, bbox_w, bbox_h, pos_x, pos_y);

    data = cJSON_GetObjectItem(client, "data");
    data_id = cJSON_GetObjectItem(data, "id");
    data_label = cJSON_GetObjectItem(data, "label");
    data_class = cJSON_GetObjectItem(data, "class");

    /* "data": {
            ...
            "parent": ""
            ...
        }
    */
    cJSON_AddItemToObject(data, "parent", cJSON_CreateString(parent_id));

    cJSON_SetValuestring(data_id, context->id);
    cJSON_SetValuestring(data_label, context->id);
    cJSON_SetValuestring(data_class, "macromolecule");

    return client;
}

static cJSON *create_topic(struct mosquitto__subhier *sub, struct mosquitto *context,
                            double bbox_x, double bbox_y,
                            double bbox_w, double bbox_h,
                            double pos_x, double pos_y) {
    cJSON *topic, *data, *data_id, *data_label, *data_class;
    topic = create_container(context, bbox_x, bbox_y, bbox_w, bbox_h, pos_x, pos_y);

    data = cJSON_GetObjectItem(topic, "data");
    data_id = cJSON_GetObjectItem(data, "id");
    data_label = cJSON_GetObjectItem(data, "label");
    data_class = cJSON_GetObjectItem(data, "class");

    cJSON_SetValuestring(data_id, sub->topic);
    cJSON_SetValuestring(data_label, sub->topic);
    cJSON_SetValuestring(data_class, "simple chemical");

    return topic;
}

int network_graph_add_node(struct mosquitto_db *db, struct mosquitto *context) {
    char *json_buf;
    cJSON *compartment, *client;

    if (find_node(context->id) == NULL) {
        compartment = create_container(context, 200, 200, 0, 0, 200, 200);
        client = create_client(context, context->address, 100, 100, 0, 0, 100, 100);

        cJSON_AddItemToArray(graph, compartment);
        cJSON_AddItemToArray(graph, client);
    }
    // else update node

    json_buf = cJSON_Print(graph);

    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 0, NULL);

    return 0;
}

int network_graph_add_subtopic(struct mosquitto_db *db, struct mosquitto *context) {
    char *json_buf;
    struct mosquitto__subhier *ptr;
    cJSON *node_topic_edge;

    assert(context->sub_count > 0);

    ptr = context->subs[context->sub_count-1]; // latest subbed topic

    if (find_edge(context->id, ptr->topic) == NULL) {
        node_topic_edge = create_edge(context, context->id, ptr->topic);
        cJSON_AddItemToArray(graph, node_topic_edge);
    }

    log__printf(NULL, MOSQ_LOG_NOTICE, "%s subbed to %s", context->id, ptr->topic);

    // traverse through parent topics: hello/test/sub_test ==> hello <- test <- sub_test
    for (; ptr != NULL && strlen(ptr->topic) > 0; ptr = ptr->parent) {
        cJSON *topic, *topic_topic_edge;

        topic = create_topic(ptr, context, 300, 100, 0, 0, 300, 100);
        cJSON_AddItemToArray(graph, topic);

        if (ptr->parent != NULL && strlen(ptr->parent->topic) > 0 &&
                find_edge(ptr->topic, ptr->parent->topic) == NULL) {
            topic_topic_edge = create_edge(context, ptr->topic, ptr->parent->topic);
            cJSON_AddItemToArray(graph, topic_topic_edge);
        }
    }

    json_buf = cJSON_Print(graph);

    db__messages_easy_queue(db, NULL, "$SYS/graph", 1, strlen(json_buf), json_buf, 1, 0, NULL);

    return 0;
}
