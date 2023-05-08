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

#include "time_log.h"

int handle__packet(struct mosquitto_db *db, struct mosquitto *context)
{
	int rc;
	if(!context) return MOSQ_ERR_INVAL;

	TIME_LOG_DECLARE();

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
			TIME_LOG_START();
			rc = handle__publish(db, context);
			TIME_LOG_END(handle__publish);
			break;
		case CMD_PUBREC:
			rc = handle__pubrec(db, context);
			break;
		case CMD_PUBREL:
			rc = handle__pubrel(db, context);
#ifdef WITH_GRAPH
			network_graph_latency_end(context);
#endif
			break;
		case CMD_CONNECT:
			TIME_LOG_START();
			rc = handle__connect(db, context);
			TIME_LOG_END(handle__connect);
			break;
		case CMD_DISCONNECT:
			rc = handle__disconnect(db, context);
			break;
		case CMD_SUBSCRIBE:
			TIME_LOG_START();
			rc = handle__subscribe(db, context);
			TIME_LOG_END(handle__subscribe);
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

	return rc;
}
