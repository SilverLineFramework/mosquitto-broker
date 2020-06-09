#ifndef NETWORK_GRAPH_H
#define NETWORK_GRAPH_H

#include "mosquitto.h"

int network_graph_init();
int network_graph_add_node(struct mosquitto_db *db, struct mosquitto *context);

#endif
