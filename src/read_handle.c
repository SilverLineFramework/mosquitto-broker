/*
Copyright (c) 2009-2020 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cJSON.h>

#include "mosquitto_broker_internal.h"
#include "mqtt_protocol.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "read_handle.h"
#include "send_mosq.h"
#include "sys_tree.h"
#include "util_mosq.h"
#include "network_graph.h"

int i = 0;

int handle__packet(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc;
	if(!context) return MOSQ_ERR_INVAL;

	switch((context->in_packet.command)&0xF0){
		case CMD_PINGREQ:
			rc = handle__pingreq(context);
			break;
		case CMD_PINGRESP:
			rc = handle__pingresp(context);
			break;
		case CMD_PUBACK:
			rc = handle__pubackcomp(db, context, "PUBACK");
			break;
		case CMD_PUBCOMP:
			rc = handle__pubackcomp(db, context, "PUBCOMP");
			break;
		case CMD_PUBLISH:
			rc = handle__publish(db, context);
			break;
		case CMD_PUBREC:
			rc = handle__pubrec(db, context);
			break;
		case CMD_PUBREL:
			rc = handle__pubrel(db, context);
			break;
		case CMD_CONNECT:
			rc = handle__connect(db, context);
			break;
		case CMD_DISCONNECT:
			rc = handle__disconnect(db, context);
			break;
		case CMD_SUBSCRIBE:
			rc = handle__subscribe(db, context);
			break;
		case CMD_UNSUBSCRIBE:
			rc = handle__unsubscribe(db, context);
			break;
#ifdef WITH_BRIDGE
		case CMD_CONNACK:
			rc = handle__connack(db, context);
			break;
		case CMD_SUBACK:
			rc = handle__suback(context);
			break;
		case CMD_UNSUBACK:
			rc = handle__unsuback(context);
			break;
#endif
		case CMD_AUTH:
			rc = handle__auth(db, context);
			break;
		default:
			/* If we don't recognize the command, return an error straight away. */
			return MOSQ_ERR_PROTOCOL;
	}

	++i;
	log__printf(NULL, MOSQ_LOG_NOTICE, "ip addr -> %s", context->address);
	log__printf(NULL, MOSQ_LOG_NOTICE, "client id -> %s", context->id);
	if (context->sub_count > 0) {
		for (int j = 0; j < context->sub_count; j++)
			log__printf(NULL, MOSQ_LOG_NOTICE, "topic -> %s", context->subs[j]->topic);
	}
	log__printf(NULL, MOSQ_LOG_NOTICE, "remaining len -> %d", context->in_packet.remaining_length);

	cJSON *root, *data1, *data2, *conn, *_data1, *_data2, *_conn;

    root = cJSON_CreateArray();
    data1 = cJSON_CreateObject();
    data2 = cJSON_CreateObject();
    conn = cJSON_CreateObject();
    _data1 = cJSON_CreateObject();
    _data2 = cJSON_CreateObject();
    _conn = cJSON_CreateObject();

    cJSON_AddItemToObject(data1, "data", _data1);
    cJSON_AddItemToObject(_data1, "id", cJSON_CreateString("a"));
    cJSON_AddItemToObject(data2, "data", _data2);
    cJSON_AddItemToObject(_data2, "id", cJSON_CreateString("b"));
    cJSON_AddItemToObject(conn, "data", _conn);
    cJSON_AddItemToObject(_conn, "id", cJSON_CreateString("ab"));
    cJSON_AddItemToObject(_conn, "source", cJSON_CreateString("a"));
    cJSON_AddItemToObject(_conn, "target", cJSON_CreateString("b"));

    cJSON_AddItemToArray(root, data1);
    cJSON_AddItemToArray(root, data2);
    cJSON_AddItemToArray(root, conn);

    char *out = cJSON_Print(root);

	db__messages_easy_queue(db, NULL, "$SYS/hello", 1, strlen(out), out, 1, 60, NULL);

	return rc;
}
