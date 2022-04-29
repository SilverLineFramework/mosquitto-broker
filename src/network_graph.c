#include <string.h>
#include <math.h>
#include <cJSON/cJSON.h>
#include <time.h>

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

#define GRAPH_QOS       2
#define BUFLEN          100

#define ID_STR_LEN      26
#define ID_CHARS_LEN    62

/*****************************************************************************/

static struct network_graph *graph = NULL;      /**< global graph structure */
static cJSON_Hooks *hooks = NULL;

static int ttl_cnt = 0;

static unsigned long memcount = 0;
static unsigned long max_memcount = 0;

static char id_chars[] = "1234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

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

static char *create_random_id(void) {
    char *id = (char *)malloc(ID_STR_LEN * sizeof(char));
    for (int i = 0; i < ID_STR_LEN-1; i++) {
        id[i] = id_chars[random() % ID_CHARS_LEN];
    }
    id[ID_STR_LEN-1] = '\0';
    return id;
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
 * Creates an ip container struct from a given IP address
 */
static struct ip_container *create_ip_container(const char *ip_addr) {
    struct ip_container *ip_cont = (struct ip_container *)graph__malloc(sizeof(struct ip_container));
    if (!ip_cont) {
        return NULL;
    }
    ip_cont->next = NULL;
    ip_cont->prev = NULL;

    ip_cont->id = create_random_id();

    ip_cont->client_dict = (struct client_dict *)graph__malloc(sizeof(struct client_dict));
    if (!ip_cont->client_dict) {
        graph__free(ip_cont->id);
        graph__free(ip_cont);
        return NULL;
    }

    ip_cont->client_dict->client_list = (struct client **)graph__calloc(1, sizeof(struct client *));
    if (!ip_cont->client_dict->client_list) {
        graph__free(ip_cont->id);
        graph__free(ip_cont->client_dict);
        graph__free(ip_cont);
        return NULL;
    }
    ip_cont->client_dict->max_size = 1;
    ip_cont->client_dict->used = 0;

    ip_cont->hash = sdbm_hash(ip_addr);
    return ip_cont;
}

/*
 * Creates an client struct from a given client id and ip address
 */
static struct client *create_client(const char *id, const char *address) {
    struct client *client = (struct client *)graph__malloc(sizeof(struct client));
    if (!client) return NULL;
    client->latency = NAN;
    client->time_prev = -1;
    client->name = graph__strdup(id);
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
    topic->name = graph__strdup(name);
    topic->next = NULL;
    topic->prev = NULL;
    topic->sub_list = NULL;
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
static int graph_add_ip(struct ip_container *ip_cont) {
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
    unsigned long hash = sdbm_hash(id),
                   idx = hash % ip_cont->client_dict->max_size;
    struct client *client = ip_cont->client_dict->client_list[idx];
    for (; client != NULL; client = client->next) {
        if (hash == client->hash) {
            return client;
        }
    }
    return NULL;
}

/*
 * Change the size of the client hash table
 */
static int graph_set_client_dict_size(struct ip_container *ip_cont, unsigned int new_size) {
    size_t idx;
    struct client **client_list_copy, *temp;
    client_list_copy = ip_cont->client_dict->client_list;
    ip_cont->client_dict->client_list = (struct client **)graph__calloc(new_size, sizeof(struct client *));
    if (ip_cont->client_dict->client_list == NULL) return -1;

    // rehash all elems of old dict and place them in new dict
    for (size_t i = 0; i < ip_cont->client_dict->max_size; ++i) {
        while (client_list_copy[i] != NULL) {
            temp = client_list_copy[i];
            client_list_copy[i] = client_list_copy[i]->next;
            idx = temp->hash % new_size;
            temp->prev = NULL;
            temp->next = ip_cont->client_dict->client_list[idx];
            if (ip_cont->client_dict->client_list[idx] != NULL) {
                ip_cont->client_dict->client_list[idx]->prev = temp;
            }
            ip_cont->client_dict->client_list[idx] = temp;
        }
    }
    ip_cont->client_dict->max_size = new_size;

    graph__free(client_list_copy);

    return 0;
}

/*
 * Adds a client to an IP container
 */
static int graph_add_client(struct ip_container *ip_cont, struct client *client) {
    size_t idx;
    if (ip_cont->client_dict->used == ip_cont->client_dict->max_size) {
        graph_set_client_dict_size(ip_cont, ip_cont->client_dict->max_size * 2);
    }

    idx = client->hash % ip_cont->client_dict->max_size;
    client->next = ip_cont->client_dict->client_list[idx];
    if (ip_cont->client_dict->client_list[idx] != NULL) {
        ip_cont->client_dict->client_list[idx]->prev = client;
    }
    ip_cont->client_dict->client_list[idx] = client;
    ++ip_cont->client_dict->used;
    return 0;
}
/*
 * Searches for a published topic
 */
static struct topic *find_topic(const char *topic) {
    unsigned long hash = sdbm_hash(topic),
                        idx = hash % graph->topic_dict->max_size;
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
 * Delete all the subcription edges of a topic
 */
static int graph_delete_topic_sub_edges(struct topic *topic) {
    struct sub_edge *curr = topic->sub_list, *temp;
    while (curr != NULL) {
        temp = curr;
        curr = curr->next;
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
 * Delete a topic
 */
static int graph_delete_topic(struct topic *topic) {
    graph_detach_topic(topic);
    graph_delete_topic_sub_edges(topic);
    graph__free(topic->name);
    graph__free(topic);

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
 * Delete a sub edge from the sub list
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
    graph__free(sub_edge);
    return 0;
}

/*
 * Delete a sub edge from the sub list
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
        // retained messages get deleted after an interval
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
 * Delete all the publish edges of a client
 */
static int graph_delete_client_pub_edges(struct client *client) {
    struct pub_edge *curr = client->pub_list, *temp;
    while (curr != NULL) {
        temp = curr;
        curr = curr->next;
        pub_edge_decr_ref_cnt(temp);
        graph__free(temp);
    }
    client->pub_list = NULL;
    return 0;
}

/*
 * Delete a pub edge from the pub list
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
    graph__free(pub_edge);
    return 0;
}

/*
 * Delete a pub edge from the pub list
 */
static int graph_delete_pub_edge(struct client *client, struct topic *topic) {
    struct pub_edge *pub_edge = find_pub_edge(client, topic);
    if (pub_edge == NULL) return -1;
    graph_delete_pub(client, pub_edge);
    return 0;
}

/*
 * Delete an IP container
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
    graph__free(ip_cont->id);
    graph__free(ip_cont->client_dict->client_list);
    graph__free(ip_cont->client_dict);
    graph__free(ip_cont);

    if (graph->ip_dict->used < graph->ip_dict->max_size / 4) {
        graph_set_ip_dict_size(graph->ip_dict->max_size / 2);
    }
    --graph->ip_dict->used;
    return 0;
}

/*
 * Delete a client
 */
static int graph_delete_client(struct ip_container *ip_cont, struct client *client) {
    struct topic *curr;
    size_t idx = client->hash % ip_cont->client_dict->max_size;
    if (ip_cont->client_dict->client_list[idx] == client) {
        ip_cont->client_dict->client_list[idx] = ip_cont->client_dict->client_list[idx]->next;
    }
    if (client->next != NULL) {
        client->next->prev = client->prev;
    }
    if (client->prev != NULL) {
        client->prev->next = client->next;
    }

    // unlink client from all subbed topics
    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        curr = graph->topic_dict->topic_list[i];
        for (; curr != NULL; curr = curr->next) {
            graph_delete_sub_edge(curr, client);
        }
    }

    graph_delete_client_pub_edges(client);
    graph__free(client->name);
    graph__free(client);

    if (ip_cont->client_dict->used < ip_cont->client_dict->max_size / 4) {
        graph_set_client_dict_size(ip_cont, ip_cont->client_dict->max_size / 2);
    }
    --ip_cont->client_dict->used;

    // if IP container is empty, delete it
    if (ip_cont->client_dict->used == 0) {
        graph_delete_ip(ip_cont);
    }
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
    struct client *client_curr, *client_temp;
    struct topic *topic_curr, *topic_temp;

    for (size_t i = 0; i < graph->ip_dict->max_size; ++i) {
        ip_curr = graph->ip_dict->ip_list[i];
        while (ip_curr != NULL) {
            for (size_t j = 0; j < ip_curr->client_dict->max_size; ++j) {
                client_curr = ip_curr->client_dict->client_list[j];
                while (client_curr != NULL) {
                    client_temp = client_curr;
                    client_curr = client_curr->next;
                    graph__free(client_curr->name);
                    graph__free(client_curr);
                }
            }
            ip_temp = ip_curr;
            ip_curr = ip_curr->next;
            graph__free(ip_temp->id);
            graph__free(ip_temp->client_dict->client_list);
            graph__free(ip_temp->client_dict);
            graph__free(ip_temp);
        }
    }

    for (size_t i = 0; i < graph->topic_dict->max_size; ++i) {
        topic_curr = graph->topic_dict->topic_list[i];
        while (topic_curr != NULL) {
            topic_temp = topic_curr;
            topic_curr = topic_curr->next;
            graph_delete_topic_sub_edges(topic_temp);
            free(topic_temp->name);
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
        graph_add_ip(ip_cont);
    }

    if (find_client(ip_cont, id) == NULL) {
        graph_add_client(ip_cont, create_client(id, address));
    }

    graph->changed = true;

    return 0;
}

/*
 * Called after client publishes to topic
 */
int network_graph_add_topic(struct mosquitto *context, uint8_t retain, const char *topic, uint32_t payloadlen) {
    if (topic[0] == '$') return 0; // ignore $SYS/#, $NETWORK/# topics
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
    if ((topic_vert = find_topic(topic)) == NULL) {
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
    if (topic[0] == '$') return 0; // ignore $SYS or $NETWORK topics
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

    topic_vert = find_topic(topic);
    if (topic_vert && find_sub_edge(topic_vert, client) == NULL) {
        sub_edge = create_sub_edge(topic_vert->name, id);
        sub_edge->sub = client;
        graph_add_sub_edge(topic_vert, sub_edge);
        graph->changed = true;
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
            mosquitto_topic_matches_sub(topic, topic_vert->name, &match);
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
    // do not update latency if topic is not $NETWORK/latency
    if (strncmp(topic, "$NETWORK/latency", 15) != 0) return 0;

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

    client->time_prev = mosquitto_time();

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

    if (client->time_prev > 0) {
        client->latency = (double)(mosquitto_time() - client->time_prev);
        client->latency = round3(client->latency / 1000); // ms
        client->time_prev = -1;

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
 * Created empty graph JSON
 */
static cJSON *graph_json_create() {
    /*
    {
        "ips": [],
        "topics": []
    }
    */
    cJSON *root, *ips, *topics;
    root = cJSON_CreateObject();
    ips = cJSON_CreateArray();
    topics = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "ips", ips);
    cJSON_AddItemToObject(root, "topics", topics);
    return root;
}

/*
 * Adds an ip address to JSON
 */
static cJSON *graph_json_add_ip(cJSON *root, struct ip_container *ip) {
    /*
    {
        "id": "<random id>",
        "clients": []
    }
    */
    cJSON *ip_json, *ips;
    ips = cJSON_GetObjectItem(root, "ips");
    ip_json = cJSON_CreateObject();
    cJSON_AddStringToObject(ip_json, "id", ip->id);
    cJSON_AddItemToObject(ip_json, "clients", cJSON_CreateArray());
    cJSON_AddItemToArray(ips, ip_json);
    return ip_json;
}

/*
 * Adds a client to JSON
 */
static cJSON *ip_json_add_client(cJSON *ip_json, struct client *client) {
    /*
    {
        "name": "client1",
        "latency": 400,
        "published": []
    }
    */
    cJSON *client_json, *clients;
    clients = cJSON_GetObjectItem(ip_json, "clients");
    client_json = cJSON_CreateObject();
    cJSON_AddStringToObject(client_json, "name", client->name);
    cJSON_AddNumberToObject(client_json, "latency", client->latency);
    cJSON_AddItemToObject(client_json, "published", cJSON_CreateArray());
    cJSON_AddItemToArray(clients, client_json);
    return client_json;
}

/*
 * Adds a publisher edge to JSON
 */
static void client_json_add_pub(cJSON *client, struct pub_edge *pub_edge) {
    /*
        {
            "topic": "topic1/sub_topic1",
            "bps": 9000
        }
    */
    cJSON *topic_json, *topics;
    topics = cJSON_GetObjectItem(client, "published");
    topic_json = cJSON_CreateObject();
    cJSON_AddStringToObject(topic_json, "topic", pub_edge->pub->name);
    cJSON_AddNumberToObject(topic_json, "bps", pub_edge->bytes_per_sec);
    cJSON_AddItemToArray(topics, topic_json);
}

/*
 * Adds a subscription edge to JSON
 */
static cJSON *graph_json_add_topic(cJSON *root, struct topic *topic) {
    /*
    {
        "name": "topic1/sub_topic1",
        "subscriptions": []
    }
    */
    cJSON *topic_json, *topics;
    topics = cJSON_GetObjectItem(root, "topics");
    topic_json = cJSON_CreateObject();
    cJSON_AddStringToObject(topic_json, "name", topic->name);
    cJSON_AddItemToObject(topic_json, "subscriptions", cJSON_CreateArray());
    cJSON_AddItemToArray(topics, topic_json);
    return topic_json;
}

/*
 * Adds an ip address to JSON
 */
static void topic_json_add_sub(cJSON *topic, struct sub_edge *sub_edge, double bytes_per_sec) {
    /*
    {
        "client": "client1",
        "bps": 9000
    }
    */
    cJSON *sub_json, *subs;
    subs = cJSON_GetObjectItem(topic, "subscriptions");
    sub_json = cJSON_CreateObject();
    cJSON_AddStringToObject(sub_json, "client", sub_edge->sub->name);
    cJSON_AddNumberToObject(sub_json, "bps", bytes_per_sec);
    cJSON_AddItemToArray(subs, sub_json);
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
    cJSON *root, *ip_json, *client_json, *topic_json;

    struct ip_container *ip_cont;
    struct client *client;
    struct sub_edge *sub_edge;
    struct pub_edge *pub_edge, *curr_pub_edge;
    struct topic *topic, *temp;

    double temp_bytes;

    if (interval && now - interval > last_update) {
        root = graph_json_create();

        // graph has a list of all IP addresses
        for (size_t i = 0; i < graph->ip_dict->max_size; ++i) {
            ip_cont = graph->ip_dict->ip_list[i];

            for (; ip_cont != NULL; ip_cont = ip_cont->next) {
                ip_json = graph_json_add_ip(root, ip_cont);

                for (size_t j = 0; j < ip_cont->client_dict->max_size; ++j) {
                    client = ip_cont->client_dict->client_list[j];

                    for (; client != NULL; client = client->next) {
                        client_json = ip_json_add_client(ip_json, client);

                        pub_edge = client->pub_list;
                        while (pub_edge != NULL) { // client may have published topics
                            curr_pub_edge = pub_edge;
                            pub_edge = pub_edge->next;

                            temp_bytes = (double)curr_pub_edge->bytes / (now - last_update);
                            curr_pub_edge->bytes = 0;
                            curr_pub_edge->bytes_per_sec = round3(temp_bytes);

                            if (curr_pub_edge->bytes_per_sec == 0.0 && --curr_pub_edge->ttl_cnt <= 0) {
                                graph_delete_pub(client, curr_pub_edge);
                                graph->changed = true;
                            }
                            else {
                                client_json_add_pub(client_json, curr_pub_edge);
                            }
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
                    topic_json = graph_json_add_topic(root, topic);

                    // update incoming bytes/s to topic
                    temp_bytes = (double)topic->bytes / (now - last_update);
                    topic->bytes = 0;
                    topic->bytes_per_sec = round3(temp_bytes);

                    sub_edge = topic->sub_list;
                    // each topic has a list of subscribed clients
                    for (; sub_edge != NULL; sub_edge = sub_edge->next) {
                        topic_json_add_sub(topic_json, sub_edge, topic->bytes_per_sec);
                    }
                    topic = topic->next;
                }
            }
        }

        if (graph->topic_dict->used < graph->topic_dict->max_size / 4) {
            graph_set_topic_dict_size(graph->topic_dict->max_size / 2);
        }

        // publish the updated graph to $NETWORK topic
        json_buf = cJSON_PrintUnformatted(root);
        if (json_buf != NULL && graph->changed) {
            db__messages_easy_queue(db, NULL, "$NETWORK", GRAPH_QOS, strlen(json_buf), json_buf, 1, 0, NULL);
            graph->changed = false;
        }
        cJSON_free(json_buf);
        cJSON_Delete(root);

        // update current graph memory usage topic
        if (current_heap != memcount) {
            current_heap = memcount;
            snprintf(heap_buf, BUFLEN, "%lu", current_heap);
            db__messages_easy_queue(db, NULL, "$NETWORK/heap/current", GRAPH_QOS, strlen(heap_buf), heap_buf, 1, 60, NULL);
        }

        // update current graph maximum memory usage topic
        if (max_heap != memcount) {
            max_heap = max_memcount;
            snprintf(heap_buf, BUFLEN, "%lu", max_heap);
            db__messages_easy_queue(db, NULL, "$NETWORK/heap/maximum", GRAPH_QOS, strlen(heap_buf), heap_buf, 1, 60, NULL);
        }

        last_update = mosquitto_time();
    }
}
