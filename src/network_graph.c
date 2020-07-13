#include <string.h>
#include <math.h>
#include <../cJSON/cJSON.h>

#  if defined(__APPLE__)
#    include <malloc/malloc.h>
#    define malloc_usable_size malloc_size
#  elif defined(__FreeBSD__)
#    include <malloc_np.h>
#  else
#    include <malloc.h>
#  endif

#include "mosquitto_broker_internal.h"
#include "mosquitto.h"
#include "memory_mosq.h"
#include "sys_tree.h"
#include "network_graph.h"

#define GRAPH_QOS   2
#define BUFLEN      100

// Useful constants //
const char s[2] = "/";
const char *nodes = "nodes";
const char *ip_class = "ip";
const char *client_class = "client";
const char *topic_class = "topic";

/*****************************************************************************/

static struct network_graph *graph = NULL;
static cJSON_Hooks *hooks = NULL;
static int ttl_cnt = 0;
static unsigned long memcount = 0;
static unsigned long max_memcount = 0;

/*****************************************************************************/

static inline double round3(double num) {
    return (double)((int)(num * 1000 + 0.5)) / 1000;
}

/*
 * General purpose hash function
 */
static unsigned long sdbm_hash(const char *str) {
    if (str == NULL) return 0;
    unsigned long hash = -1, c;
    while((c = *str++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

/*
 * Malloc wrapper for counting graph memory usage
 */
void *graph__malloc(size_t len) {
    void *mem = malloc(len);
    if (mem != NULL) {
        memcount += malloc_usable_size(mem);
        if (memcount > max_memcount){
			max_memcount = memcount;
		}
    }
    return mem;
}

/*
 * Calloc wrapper for counting graph memory usage
 */
void *graph__calloc(size_t nmemb, size_t size) {
	void *mem = calloc(nmemb, size);
    if (mem != NULL) {
        memcount += malloc_usable_size(mem);
        if (memcount > max_memcount){
			max_memcount = memcount;
		}
    }
    return mem;
}

/*
 * Free wrapper for counting graph memory usage
 */
void graph__free(void *mem) {
	if (mem == NULL) {
		return;
	}
	memcount -= malloc_usable_size(mem);
	free(mem);
}

/*
 * Strdup wrapper for counting graph memory usage
 */
char *graph__strdup(const char *s) {
	char *str = strdup(s);
	if (str != NULL) {
		memcount += malloc_usable_size(str);
        if (memcount > max_memcount){
			max_memcount = memcount;
		}
	}
	return str;
}

/*****************************************************************************/

/*
 * Creates a template cJSON struct to be used by all nodes/edges
 */
static cJSON *create_generic_json(const char *id) {
    cJSON *root, *data;
    root = cJSON_CreateObject();
    data = cJSON_CreateObject();

    cJSON_AddItemToObject(data, "id", cJSON_CreateString(id));
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
    cJSON_AddItemToObject(root, "group", cJSON_CreateString(nodes));

    return root;
}

/*
 * Creates a cJSON struct for an edge
 */
static cJSON *create_edge_json(const char *node1, const char *node2) {
    cJSON *root, *data;
    char buf[BUFLEN];
    static unsigned int i = 0;

    snprintf(buf, BUFLEN, "edge_%d", i++);
    root = create_generic_json(buf);
    data = cJSON_GetObjectItem(root, "data");

    cJSON_AddNumberToObject(data, "bps", 0.0);
    cJSON_AddItemToObject(data, "source", cJSON_CreateString(node1));
    cJSON_AddItemToObject(data, "target", cJSON_CreateString(node2));
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

    cJSON_AddNumberToObject(data, "latency", NAN);
    cJSON_AddItemToObject(data, "class", cJSON_CreateString(client_class));
    cJSON_AddItemToObject(data, "parent", cJSON_CreateString(parent));
    cJSON_AddItemToObject(root, "group", cJSON_CreateString(nodes));

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
    cJSON_AddItemToObject(root, "group", cJSON_CreateString(nodes));

    return root;
}

/*
 * Creates an ip container struct from a given IP address
 */
static struct ip_container *create_ip_container(const char *ip) {
    struct ip_container *ip_cont = (struct ip_container *)graph__malloc(sizeof(struct ip_container));
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
    struct client *client = (struct client *)graph__malloc(sizeof(struct client));
    if (!client) return NULL;
    client->latency_ready = false;
    client->latency_total = 0;
    client->latency_cnt = 0;
    client->json = create_client_json(id, address);
    client->pub_list = NULL;
    client->next = NULL;
    client->prev = NULL;
    client->hash = sdbm_hash(id);
    return client;
}

/*
 * Creates an topic struct from a given topic name
 */
static struct topic *create_topic(const char *name, uint8_t retain) {
    struct topic *topic = (struct topic *)graph__malloc(sizeof(struct topic));
    if (!topic) return NULL;
    topic->retain = retain;
    topic->ttl_cnt = ttl_cnt;
    topic->json = create_topic_json(name);
    topic->next = NULL;
    topic->prev = NULL;
    topic->sub_list = NULL;
    topic->full_name = graph__strdup(name);
    topic->hash = sdbm_hash(name);
    topic->ref_cnt = 0;
    topic->bytes = 0;
    topic->bytes_per_sec = 0.0;
    return topic;
}

/*
 * Creates an subscriber edge struct from source and target.
 * subscribe == topic -> client
 * src should be a topic, tgt should be a client.
 */
static struct sub_edge *create_sub_edge(const char *src, const char *tgt) {
    struct sub_edge *sub_edge = (struct sub_edge *)graph__malloc(sizeof(struct sub_edge));
    if (!sub_edge) return NULL;
    sub_edge->json = create_edge_json(src, tgt);
    sub_edge->sub = NULL;
    sub_edge->next = NULL;
    sub_edge->prev = NULL;
    return sub_edge;
}

/*
 * Creates a publisher edge struct from source and target.
 * publish == client -> topic
 * src should be a client, tgt should be a topic.
 */
static struct pub_edge *create_pub_edge(const char *src, const char *tgt, struct topic *topic) {
    if (topic == NULL) return NULL;
    struct pub_edge *pub_edge = (struct pub_edge *)graph__malloc(sizeof(struct pub_edge));
    if (!pub_edge) return NULL;
    pub_edge->ttl_cnt = ttl_cnt;
    pub_edge->json = create_edge_json(src, tgt);
    pub_edge->pub = topic;
    pub_edge->next = NULL;
    pub_edge->prev = NULL;
    pub_edge->bytes = 0;
    pub_edge->bytes_per_sec = 0;
    return pub_edge;
}

/*
 * Searches for an IP container given an address
 */
static struct ip_container *find_ip_container(const char *address) {
    unsigned long hash = sdbm_hash(address),
                   idx = hash % graph->ip_dict->max_size;
    struct ip_container *ip_cont = graph->ip_dict->ip_list[idx];
    for (; ip_cont != NULL; ip_cont = ip_cont->next) {
        if (hash == ip_cont->hash) {
            return ip_cont;
        }
    }
    return NULL;
}

/*
 * Change the size of the ip hash table
 */
static int graph_set_ip_dict_size(unsigned int new_size) {
    size_t idx;
    struct ip_container **ip_list_copy, *temp;
    ip_list_copy = graph->ip_dict->ip_list;
    graph->ip_dict->ip_list = (struct ip_container **)graph__calloc(new_size, sizeof(struct ip_container *));
    if (graph->ip_dict->ip_list == NULL) return -1;

    // rehash all elems of old dict and place them in new dict
    for (size_t i = 0; i < graph->ip_dict->max_size; ++i) {
        while (ip_list_copy[i] != NULL) {
            temp = ip_list_copy[i];
            ip_list_copy[i] = ip_list_copy[i]->next;
            idx = temp->hash % new_size;
            temp->prev = NULL;
            temp->next = graph->ip_dict->ip_list[idx];
            if (graph->ip_dict->ip_list[idx] != NULL) {
                graph->ip_dict->ip_list[idx]->prev = temp;
            }
            graph->ip_dict->ip_list[idx] = temp;
        }
    }
    graph->ip_dict->max_size = new_size;

    graph__free(ip_list_copy);

    return 0;
}

/*
 * Adds an IP container to the network graph
 */
static int graph_add_ip_container(struct ip_container *ip_cont) {
    size_t idx;
    if (graph->ip_dict->used == graph->ip_dict->max_size) {
        graph_set_ip_dict_size(graph->ip_dict->max_size * 2);
    }

    idx = ip_cont->hash % graph->ip_dict->max_size;
    ip_cont->next = graph->ip_dict->ip_list[idx];
    if (graph->ip_dict->ip_list[idx] != NULL) {
        graph->ip_dict->ip_list[idx]->prev = ip_cont;
    }
    graph->ip_dict->ip_list[idx] = ip_cont;
    ++graph->ip_dict->used;
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
    unsigned long hash = sdbm_hash(topic), idx = hash % graph->topic_dict->max_size;
    struct topic *curr = graph->topic_dict->topic_list[idx];
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
        if (curr->sub == client) {
            return curr;
        }
    }
    return NULL;
}

/*
 * Searches for a pub edge
 */
static struct pub_edge *find_pub_edge(struct client *client, struct topic *topic) {
    struct pub_edge *curr = client->pub_list;
    for (; curr != NULL; curr = curr->next) {
        if (curr->pub == topic) {
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
    struct topic *curr;
    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        curr = graph->topic_dict->topic_list[i];
        for (; curr != NULL; curr = curr->next) {
            mosquitto_topic_matches_sub(topic, curr->full_name, &match);
            if (match) {
                return curr;
            }
        }
    }
    return NULL;
}

/*
 * Change the size of the topic hash table
 */
static int graph_set_topic_dict_size(unsigned int new_size) {
    size_t idx;
    struct topic **topic_list_copy, *temp;
    topic_list_copy = graph->topic_dict->topic_list;
    graph->topic_dict->topic_list = (struct topic **)graph__calloc(new_size, sizeof(struct topic *));
    if (graph->topic_dict->topic_list == NULL) return -1;

    // rehash all elems of old dict and place them in new dict
    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        while (topic_list_copy[i] != NULL) {
            temp = topic_list_copy[i];
            topic_list_copy[i] = topic_list_copy[i]->next;
            idx = temp->hash % new_size;
            temp->prev = NULL;
            temp->next = graph->topic_dict->topic_list[idx];
            if (graph->topic_dict->topic_list[idx] != NULL) {
                graph->topic_dict->topic_list[idx]->prev = temp;
            }
            graph->topic_dict->topic_list[idx] = temp;
        }
    }
    graph->topic_dict->max_size = new_size;

    graph__free(topic_list_copy);

    return 0;
}

/*
 * Adds a topic to the topic list
 */
static int graph_add_topic(struct topic *topic) {
    size_t idx;
    if (graph->topic_dict->used == graph->topic_dict->max_size) {
        graph_set_topic_dict_size(graph->topic_dict->max_size * 2);
    }

    idx = topic->hash % graph->topic_dict->max_size;
    topic->next = graph->topic_dict->topic_list[idx];
    if (graph->topic_dict->topic_list[idx] != NULL) {
        graph->topic_dict->topic_list[idx]->prev = topic;
    }
    graph->topic_dict->topic_list[idx] = topic;
    ++graph->topic_dict->used;

    return 0;
}

/*
 * Deletes all the subcription edges of a topic
 */
static int graph_delete_topic_sub_edges(struct topic *topic) {
    struct sub_edge *curr = topic->sub_list, *temp;
    while (curr != NULL) {
        temp = curr;
        curr = curr->next;
        cJSON_Delete(temp->json);
        graph__free(temp);
    }
    topic->sub_list = NULL;
    return 0;
}

/*
 * Detaches a topic from the topic list
 */
static inline void graph_detach_topic(struct topic *topic) {
    size_t idx = topic->hash % graph->topic_dict->max_size;
    if (graph->topic_dict->topic_list[idx] == topic) {
        graph->topic_dict->topic_list[idx] = graph->topic_dict->topic_list[idx]->next;
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
    graph_delete_topic_sub_edges(topic);
    cJSON_Delete(topic->json);
    graph__free(topic->full_name);
    graph__free(topic);

    // if (graph->topic_dict->used < graph->topic_dict->max_size / 4) {
    //     graph_set_topic_dict_size(graph->topic_dict->max_size / 2);
    // }
    --graph->topic_dict->used;
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
static int graph_delete_sub(struct topic *topic, struct sub_edge *sub_edge) {
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
    graph__free(sub_edge);
    return 0;
}

/*
 * Deletes a sub edge from the sub list
 */
static int graph_delete_sub_edge(struct topic *topic, struct client *client) {
    struct sub_edge *sub_edge = find_sub_edge(topic, client);
    if (sub_edge == NULL) return -1;
    graph_delete_sub(topic, sub_edge);
    return 0;
}

/*
 * Adds a pub edge to the pub list
 */
static int graph_add_pub_edge(struct client *client, struct pub_edge *pub_edge) {
    pub_edge->next = client->pub_list;
    if (client->pub_list != NULL) {
        client->pub_list->prev = pub_edge;
    }
    client->pub_list = pub_edge;
    return 0;
}

/*
 * Decreased the reference count of the topic pointed to by a pub_edge
 */
static int pub_edge_decr_ref_cnt(struct pub_edge *pub_edge) {
    if (--pub_edge->pub->ref_cnt == 0) {
        if (pub_edge->pub->retain) {
            pub_edge->pub->ttl_cnt = ttl_cnt;
        }
        else {
            graph_delete_topic(pub_edge->pub);
        }
    }
    return 0;
}

/*
 * Deletes all the publish edges of a client
 */
static int graph_delete_client_pub_edges(struct client *client) {
    struct pub_edge *curr = client->pub_list, *temp;
    while (curr != NULL) {
        temp = curr;
        curr = curr->next;
        pub_edge_decr_ref_cnt(temp);
        cJSON_Delete(temp->json);
        graph__free(temp);
    }
    client->pub_list = NULL;
    return 0;
}

/*
 * Deletes a pub edge from the pub list
 */
static int graph_delete_pub(struct client *client, struct pub_edge *pub_edge) {
    if (client->pub_list == pub_edge) {
        client->pub_list = client->pub_list->next;
    }
    if (pub_edge->next != NULL) {
        pub_edge->next->prev = pub_edge->prev;
    }
    if (pub_edge->prev != NULL) {
        pub_edge->prev->next = pub_edge->next;
    }
    pub_edge_decr_ref_cnt(pub_edge);
    cJSON_Delete(pub_edge->json);
    graph__free(pub_edge);
    return 0;
}

/*
 * Deletes a pub edge from the pub list
 */
static int graph_delete_pub_edge(struct client *client, struct topic *topic) {
    struct pub_edge *pub_edge = find_pub_edge(client, topic);
    if (pub_edge == NULL) return -1;
    graph_delete_pub(client, pub_edge);
    return 0;
}

/*
 * Deletes an IP container
 */
static int graph_delete_ip(struct ip_container *ip_cont) {
    size_t idx = ip_cont->hash % graph->ip_dict->max_size;
    if (graph->ip_dict->ip_list[idx] == ip_cont) {
        graph->ip_dict->ip_list[idx] = graph->ip_dict->ip_list[idx]->next;
    }
    if (ip_cont->next != NULL) {
        ip_cont->next->prev = ip_cont->prev;
    }
    if (ip_cont->prev != NULL) {
        ip_cont->prev->next = ip_cont->next;
    }
    cJSON_Delete(ip_cont->json);
    graph__free(ip_cont);

    // if (graph->ip_dict->used < graph->ip_dict->max_size / 4) {
    //     graph_set_ip_dict_size(graph->ip_dict->max_size / 2);
    // }
    --graph->ip_dict->used;
    return 0;
}

/*
 * Delete a client
 */
static int graph_delete_client(struct ip_container *ip_cont, struct client *client) {
    struct topic *curr;
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
    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        curr = graph->topic_dict->topic_list[i];
        for (; curr != NULL; curr = curr->next) {
            graph_delete_sub_edge(curr, client);
        }
    }

    graph_delete_client_pub_edges(client);
    cJSON_Delete(client->json);
    graph__free(client);
    return 0;
}

/*****************************************************************************/

int network_graph_init(struct mosquitto_db *db) {
    if (db->config->graph_interval == 0) {
		return 0;
	}
    ttl_cnt = db->config->graph_del_mult;

    graph = (struct network_graph *)graph__malloc(sizeof(struct network_graph));
    if (!graph) return -1;

    graph->changed = false;

    graph->ip_dict = (struct ip_dict *)graph__malloc(sizeof(struct ip_dict));
    if (!graph->ip_dict) return -1;
    graph->ip_dict->ip_list = (struct ip_container **)graph__calloc(1, sizeof(struct ip_container *));
    if (!graph->ip_dict->ip_list) return -1;
    graph->ip_dict->max_size = 1;
    graph->ip_dict->used = 0;

    graph->topic_dict = (struct topic_dict *)graph__malloc(sizeof(struct topic_dict));
    if (!graph->topic_dict) return -1;
    graph->topic_dict->topic_list = (struct topic **)graph__calloc(1, sizeof(struct topic *));
    if (!graph->topic_dict->topic_list) return -1;
    graph->topic_dict->max_size = 1;
    graph->topic_dict->used = 0;

    hooks = (cJSON_Hooks *)graph__malloc(sizeof(cJSON_Hooks));
    hooks->malloc_fn = graph__malloc;
    hooks->free_fn = graph__free;
    cJSON_InitHooks(hooks);

    return 0;
}

int network_graph_cleanup(void) {
    struct ip_container *ip_curr, *ip_temp;
    struct client *client, *client_temp;
    struct topic *topic_curr, *topic_temp;

    for (size_t i = 0; i < graph->ip_dict->max_size; ++i) {
        ip_curr = graph->ip_dict->ip_list[i];
        while (ip_curr != NULL) {
            client = ip_curr->client_list;
            while (client != NULL) {
                client_temp = client;
                client = client->next;
                graph_delete_client_pub_edges(client_temp);
                cJSON_Delete(client_temp->json);
                graph__free(client_temp);
            }
            ip_temp = ip_curr;
            ip_curr = ip_curr->next;
            cJSON_Delete(ip_temp->json);
            graph__free(ip_temp);
        }
    }

    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        topic_curr = graph->topic_dict->topic_list[i];
        while (topic_curr != NULL) {
            topic_temp = topic_curr;
            topic_curr = topic_curr->next;
            graph_delete_topic_sub_edges(topic_temp);
            cJSON_Delete(topic_temp->json);
            free(topic_temp->full_name);
            graph__free(topic_temp);
        }
    }

    graph__free(graph->ip_dict->ip_list);
    graph__free(graph->ip_dict);
    graph__free(graph->topic_dict->topic_list);
    graph__free(graph->topic_dict);
    graph__free(graph);

    cJSON_free(hooks);
    return 0;
}

/*
 * Called after client connects
 */
int network_graph_add_client(struct mosquitto *context) {
    char *address, *id;
    struct ip_container *ip_cont;

    address = context->address;
#ifdef WITH_BROKER
    if (context->is_bridge && context->bridge != NULL) {
        address = context->bridge->addresses[context->bridge->cur_address].address;
    }
#endif
    id = context->id;

    if ((ip_cont = find_ip_container(address)) == NULL) {
        ip_cont = create_ip_container(address);
        graph_add_ip_container(ip_cont);
    }

    if (find_client(ip_cont, id) == NULL) {
        graph_add_client(ip_cont, create_client(context->id, address));
    }

    graph->changed = true;

    return 0;
}

/*
 * Called after client publishes to topic
 */
int network_graph_add_topic(struct mosquitto *context, uint8_t retain, const char *topic, uint32_t payloadlen) {
    if (topic[0] == '$') return 0; // ignore $SYS/#, $GRAPH/# topics
    char *address, *id;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;
    struct pub_edge *pub_edge;
    cJSON *data;

    address = context->address;
#ifdef WITH_BROKER
    if (context->is_bridge && context->bridge != NULL) {
        address = context->bridge->addresses[context->bridge->cur_address].address;
    }
#endif
    id = context->id;

    if ((ip_cont = find_ip_container(address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }

    // topic doesnt exist
    if ((topic_vert = find_pub_topic(topic)) == NULL) {
        topic_vert = create_topic(topic, retain);
        graph_add_topic(topic_vert);
        ++topic_vert->ref_cnt;
        pub_edge = create_pub_edge(id, topic, topic_vert);
        graph_add_pub_edge(client, pub_edge);
    }
    // topic exists, but pub edge doesnt
    else if ((pub_edge = find_pub_edge(client, topic_vert)) == NULL) {
        ++topic_vert->ref_cnt;
        pub_edge = create_pub_edge(id, topic, topic_vert);
        graph_add_pub_edge(client, pub_edge);
    }

    pub_edge->ttl_cnt = ttl_cnt;

    if (payloadlen > 0) {
        topic_vert->bytes += payloadlen;
        pub_edge->bytes += payloadlen;

        graph->changed = true;
    }

    return 0;
}

/*
 * Called after client subscribes to topic
 */
int network_graph_add_sub_edge(struct mosquitto *context, const char *topic) {
    if (topic[0] == '$') return 0; // ignore $SYS or $GRAPH topics
    bool match;
    char *address, *id;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;
    struct sub_edge *sub_edge;
    cJSON *data;

    address = context->address;
#ifdef WITH_BROKER
    if (context->is_bridge && context->bridge != NULL) {
        address = context->bridge->addresses[context->bridge->cur_address].address;
    }
#endif
    id = context->id;

    if ((ip_cont = find_ip_container(address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }

    // check if sub_edge already exists, if not, create sub_edge
    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        topic_vert = graph->topic_dict->topic_list[i];
        for (; topic_vert != NULL; topic_vert = topic_vert->next) {
            mosquitto_topic_matches_sub(topic, topic_vert->full_name, &match);
            if (match && find_sub_edge(topic_vert, client) == NULL) {
                sub_edge = create_sub_edge(topic_vert->full_name, id);
                sub_edge->sub = client;
                graph_add_sub_edge(topic_vert, sub_edge);
                graph->changed = true;
            }
        }
    }

    return 0;
}

/*
 * Called after client unsubscribes to topic
 */
int network_graph_delete_sub_edge(struct mosquitto *context, const char *topic) {
    bool match;
    struct ip_container *ip_cont;
    struct client *client;
    struct topic *topic_vert;

    if ((ip_cont = find_ip_container(context->address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }

    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        topic_vert = graph->topic_dict->topic_list[i];
        for (; topic_vert != NULL; topic_vert = topic_vert->next) {
            mosquitto_topic_matches_sub(topic, topic_vert->full_name, &match);
            if (match) {
                graph_delete_sub_edge(topic_vert, client);
            }
        }
    }

    graph->changed = true;

    return 0;
}

/*
 * Called after sending PUBREL
 */
int network_graph_latency_start(struct mosquitto *context, const char *topic) {
    // do not update latency if topic is not $GRAPH/latency
    if (strncmp(topic, "$GRAPH/latency", 15) != 0) return 0;

    char *address, *id;
    struct ip_container *ip_cont;
    struct client *client;

    address = context->address;
#ifdef WITH_BROKER
    if (context->is_bridge && context->bridge != NULL) {
        address = context->bridge->addresses[context->bridge->cur_address].address;
    }
#endif
    id = context->id;

    if ((ip_cont = find_ip_container(address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }

    client->latency = mosquitto_time_ns();
    client->latency_ready = true;

    return 0;
}

/*
 * Called after client sends PUBCOMP
 */
int network_graph_latency_end(struct mosquitto *context) {
    char *address, *id;
    struct ip_container *ip_cont;
    struct client *client;
    cJSON *data;
    double latency_avg_ns; // average latency in ns

    address = context->address;
#ifdef WITH_BROKER
    if (context->is_bridge && context->bridge != NULL) {
        address = context->bridge->addresses[context->bridge->cur_address].address;
    }
#endif
    id = context->id;

    if ((ip_cont = find_ip_container(address)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }

    if (client->latency_ready) {
        client->latency = (mosquitto_time_ns() - client->latency);
        client->latency_total += client->latency;
        ++client->latency_cnt;

        latency_avg_ns = (double)client->latency_total / client->latency_cnt;

        data = cJSON_GetObjectItem(client->json, "data");
        cJSON_SetNumberValue(cJSON_GetObjectItem(data, "latency"),
                             round3(latency_avg_ns / 1000)); // ns -> ms
        client->latency_ready = false;

        graph->changed = true;
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
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find ip!");
        return -1;
    }

    if ((client = find_client(ip_cont, context->id)) == NULL) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not find client!");
        return -1;
    }
    else if (graph_delete_client(ip_cont, client) < 0) {
        log__printf(NULL, MOSQ_LOG_DEBUG, "ERROR: could not delete client!");
        return -1;
    }

    graph->changed = true;

    return 0;
}

/*
 * Called every graph->interval seconds
 */
void network_graph_update(struct mosquitto_db *db, int interval) {
    static time_t last_update = 0;
    static unsigned long current_heap = -1;
    static unsigned long max_heap = -1;

    time_t now = mosquitto_time();

    char *json_buf, heap_buf[BUFLEN];
    cJSON *root, *data;

    struct ip_container *ip_cont;
    struct client *client;
    struct sub_edge *sub_edge;
    struct pub_edge *pub_edge, *pub_edge_temp;
    struct topic *topic, *temp;

    double temp_bytes;

    if (interval && now - interval > last_update) {
        root = cJSON_CreateArray();

        // graph has a list of all IP addresses
        for (size_t i = 0; i < graph->ip_dict->max_size; ++i) {
            ip_cont = graph->ip_dict->ip_list[i];

            for (; ip_cont != NULL; ip_cont = ip_cont->next) {
                cJSON_AddItemToArray(root, cJSON_Duplicate(ip_cont->json, true));

                client = ip_cont->client_list; // each IP address holds a list of clients
                for (; client != NULL; client = client->next) {
                    cJSON_AddItemToArray(root, cJSON_Duplicate(client->json, true));

                    pub_edge = client->pub_list;
                    while (pub_edge != NULL) { // client may have published topics
                        pub_edge_temp = pub_edge;
                        pub_edge = pub_edge->next;

                        temp_bytes = (double)pub_edge_temp->bytes / (now - last_update);
                        pub_edge_temp->bytes = 0;
                        pub_edge_temp->bytes_per_sec = round3(temp_bytes);

                        if (pub_edge_temp->bytes_per_sec == 0.0 && --pub_edge_temp->ttl_cnt <= 0) {
                            graph_delete_pub(client, pub_edge_temp);
                            graph->changed = true;
                        }
                        else {
                            // update outgoing bytes/s from client and add json
                            data = cJSON_GetObjectItem(pub_edge_temp->json, "data");
                            cJSON_SetNumberValue(cJSON_GetObjectItem(data, "bps"), pub_edge_temp->bytes_per_sec);
                            cJSON_AddItemToArray(root, cJSON_Duplicate(pub_edge_temp->json, true));
                        }
                    }
                }
            }
        }

        // graph has a list of all topics
        for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
            topic = graph->topic_dict->topic_list[i];
            while (topic != NULL) {
                // only delete after ttl_cnt <= 0 if retained
                if (topic->retain && topic->ref_cnt == 0 && --topic->ttl_cnt <= 0) {
                    temp = topic;
                    topic = topic->next;
                    graph_delete_topic(temp);
                    graph->changed = true;
                }
                else {
                    cJSON_AddItemToArray(root, cJSON_Duplicate(topic->json, true));

                    // update incoming bytes/s to topic
                    temp_bytes = (double)topic->bytes / (now - last_update);
                    topic->bytes = 0;
                    topic->bytes_per_sec = round3(temp_bytes);

                    sub_edge = topic->sub_list;
                    // each topic has a list of subscribed clients
                    for (; sub_edge != NULL; sub_edge = sub_edge->next) {
                        // update outgoing bytes/s from topic
                        data = cJSON_GetObjectItem(sub_edge->json, "data");
                        cJSON_SetNumberValue(cJSON_GetObjectItem(data, "bps"), topic->bytes_per_sec);
                        cJSON_AddItemToArray(root, cJSON_Duplicate(sub_edge->json, true));
                    }
                    topic = topic->next;
                }
            }
        }

        // have to resize here instead of in delete_topic
        // if (graph->topic_dict->used < graph->topic_dict->max_size / 4) {
        //     graph_set_topic_dict_size(graph->topic_dict->max_size / 2);
        // }
        // --graph->topic_dict->used;

        // send out the updated graph to $GRAPH topic
        json_buf = cJSON_PrintUnformatted(root);
        if (json_buf != NULL && graph->changed) {
            db__messages_easy_queue(db, NULL, "$GRAPH", GRAPH_QOS, strlen(json_buf), json_buf, 1, 0, NULL);
            // log__printf(NULL, MOSQ_LOG_DEBUG, "%s", json_buf);
            graph->changed = false;
        }
        cJSON_free(json_buf);
        cJSON_Delete(root);

        if (current_heap != memcount) {
            current_heap = memcount;
            snprintf(heap_buf, BUFLEN, "%lu", current_heap);
            db__messages_easy_queue(db, NULL, "$GRAPH/heap/current", GRAPH_QOS, strlen(heap_buf), heap_buf, 1, 60, NULL);
        }

        if (max_heap != memcount) {
            max_heap = max_memcount;
            snprintf(heap_buf, BUFLEN, "%lu", max_heap);
            db__messages_easy_queue(db, NULL, "$GRAPH/heap/maximum", GRAPH_QOS, strlen(heap_buf), heap_buf, 1, 60, NULL);
        }

        last_update = mosquitto_time();
    }
}
