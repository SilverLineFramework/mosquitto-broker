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

#include "mosquitto_broker_internal.h"
#include "mqtt_protocol.h"
#include "memory_mosq.h"
#include "packet_mosq.h"
#include "read_handle.h"
#include "send_mosq.h"
#include "sys_tree.h"
#include "util_mosq.h"
#include "network_graph.h"

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
			if (rc != MOSQ_ERR_PROTOCOL)
				network_graph_add_client(context);
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
			rc = MOSQ_ERR_PROTOCOL;
			break;
	}

	if (rc != MOSQ_ERR_PROTOCOL)
		network_graph_pub(db);
	else
		log__printf(NULL, MOSQ_LOG_NOTICE, "protocol error!");


	return rc;
}
