#include <margo.h>
#include <openssl/md5.h>
#include "config.h"
#include "ring_types.h"
#include "ring_list.h"
#include "log.h"

struct ring_node {
	char *address;
	char *name;
	uint8_t md5[MD5_DIGEST_LENGTH];
};

static struct ring_list {
	int n;
	struct ring_node *nodes;
} ring_list;

static char *ring_list_self;
static int ring_list_self_index;
static ABT_mutex ring_list_mutex;

static char *
address_name_dup(char *address, char *name)
{
	int addrlen, namelen;
	char *r;

	if (address == NULL)
		return (NULL);
	addrlen = strlen(address);
	if (name != NULL)
		namelen = strlen(name);
	else
		namelen = 0;
#ifndef ENABLE_HASH_PORT
	int s = addrlen - 1;
	while (s >= 0 && address[s] != ':')
		--s;
	if (s >= 0 && address[s] == ':')
		addrlen = s;
#endif
	r = malloc(addrlen + 1 + namelen + 1);
	if (r == NULL)
		return (r);
	memcpy(r, address, addrlen);
	r[addrlen++] = ':';
	if (namelen > 0)
		strcpy(r + addrlen, name);
	else
		r[addrlen] = '\0';
	return (r);
}

void
ring_list_init(char *self)
{
	node_list_t n;
	static const char diag[] = "ring_list_init";

	ABT_mutex_create(&ring_list_mutex);
	if (self == NULL) {
		ring_list.n = 0;
		ring_list.nodes = NULL;
		ring_list_self = NULL;
		ring_list_self_index = -1;
		return;
	}
	n.n = 1;
	n.s = malloc(sizeof(n.s[0]));
	if (n.s == NULL)
		log_fatal("%s: no memory", diag);
	n.s[0].address = self;
	n.s[0].name = NULL;
	ring_list_update(&n, 0);
	free(n.s);

	ring_list_self = strdup(self);
	if (ring_list_self == NULL)
		log_fatal("%s: no memory", diag);
	ring_list_self_index = 0;
}

static void
ring_list_display_node(struct ring_node *node)
{
	int i;

	printf("%s %s ", node->address, node->name);
	for (i = 0; i < MD5_DIGEST_LENGTH; ++i)
		printf("%02X", node->md5[i]);
	printf("\n");
}

void
ring_list_display()
{
	int i;

	ABT_mutex_lock(ring_list_mutex);
	for (i = 0; i < ring_list.n; ++i)
		ring_list_display_node(&ring_list.nodes[i]);
	ABT_mutex_unlock(ring_list_mutex);
}

static void
ring_list_clear()
{
	int i;

	for (i = 0; i < ring_list.n; ++i) {
		free(ring_list.nodes[i].address);
		free(ring_list.nodes[i].name);
	}
	free(ring_list.nodes);
}

int
ring_list_cmp(const void *a1, const void *a2)
{
	const struct ring_node *n1 = a1, *n2 = a2;

	return (memcmp(n1->md5, n2->md5, MD5_DIGEST_LENGTH));
}

void
ring_list_copy(node_list_t *list)
{
	int i;

	ABT_mutex_lock(ring_list_mutex);
	list->n = ring_list.n;
	list->s = malloc(sizeof(list->s[0]) * ring_list.n);
	if (list->s == NULL) {
		log_error("ring_list_copy: no memory");
		list->n = 0;
	}
	for (i = 0; i < list->n; ++i) {
		list->s[i].address = strdup(ring_list.nodes[i].address);
		list->s[i].name = strdup(ring_list.nodes[i].name);
		if (list->s[i].address == NULL || list->s[i].name == NULL)
			log_error("ring_list_copy: no memory");
	}
	ABT_mutex_unlock(ring_list_mutex);
}

void
ring_list_copy_free(node_list_t *list)
{
	int i;

	for (i = 0; i < list->n; ++i) {
		free(list->s[i].address);
		free(list->s[i].name);
	}
	free(list->s);
}

/*
 * flag: 0 - server, 1 - client
 */
void
ring_list_update(node_list_t *src, int flag)
{
	int i;

	ABT_mutex_lock(ring_list_mutex);
	ring_list_clear();
	ring_list.n = src->n;
	ring_list.nodes = malloc(sizeof(ring_list.nodes[0]) * src->n);
	if (ring_list.nodes == NULL) {
		log_error("ring_list_update: no memory");
		ring_list.n = 0;
		ring_list_self_index = -1;
		goto unlock;
	}
	for (i = 0; i < src->n; ++i) {
		ring_list.nodes[i].address = strdup(src->s[i].address);
		if (flag == 0)
			/* for server */
			ring_list.nodes[i].name =
				address_name_dup(src->s[i].address,
					src->s[i].name);
		else
			/* for client */
			ring_list.nodes[i].name =
				strdup(src->s[i].name);
		if (ring_list.nodes[i].address == NULL ||
			ring_list.nodes[i].name == NULL)
			log_fatal("ring_list_update: no memory");
		MD5((unsigned char *)ring_list.nodes[i].name,
			strlen(ring_list.nodes[i].name),
			ring_list.nodes[i].md5);
	}
	qsort(ring_list.nodes, ring_list.n, sizeof(ring_list.nodes[0]),
		ring_list_cmp);
	if (ring_list_self == NULL)
		goto unlock;
	for (i = 0; i < src->n; ++i)
		if (strcmp(ring_list.nodes[i].address, ring_list_self) == 0)
			break;
	if (i < src->n)
		ring_list_self_index = i;
	else {
		log_notice("ring_list_update: no self server");
		ring_list_self_index = -1;
	}
unlock:
	ABT_mutex_unlock(ring_list_mutex);
}

void
ring_list_remove(char *host)
{
	int i;

	if (host == NULL)
		return;
	ABT_mutex_lock(ring_list_mutex);
	for (i = 0; i < ring_list.n; ++i)
		if (strcmp(host, ring_list.nodes[i].address) == 0)
			break;
	if (i < ring_list.n) {
		free(ring_list.nodes[i].address);
		free(ring_list.nodes[i].name);
		--ring_list.n;
		for (; i < ring_list.n; ++i)
			ring_list.nodes[i] = ring_list.nodes[i + 1];
	}
	if (ring_list.n == 0) {
		log_warning("ring_list_remove: no server");
		free(ring_list.nodes);
		ring_list.nodes = NULL;
	}
	ABT_mutex_unlock(ring_list_mutex);
}

int
ring_list_is_in_charge(const char *key, int key_size)
{
	uint8_t md5[MD5_DIGEST_LENGTH];
	int r = 1;

	MD5((const unsigned char *)key, key_size, md5);
	ABT_mutex_lock(ring_list_mutex);
	if (ring_list_self_index > 0)
		r = (memcmp(&ring_list.nodes[ring_list_self_index - 1].md5, md5,
			MD5_DIGEST_LENGTH) < 0 &&
			memcmp(md5, &ring_list.nodes[ring_list_self_index].md5,
				MD5_DIGEST_LENGTH) <= 0);
	else if (ring_list_self_index == 0)
		r = (memcmp(&ring_list.nodes[ring_list.n - 1].md5, md5,
			MD5_DIGEST_LENGTH) < 0 ||
			memcmp(md5, &ring_list.nodes[0].md5,
				MD5_DIGEST_LENGTH) <= 0);
	ABT_mutex_unlock(ring_list_mutex);
	return (r);
}

static char *
ring_list_lookup_linear(const char *key, int key_size)
{
	uint8_t md5[MD5_DIGEST_LENGTH];
	char *r;
	int i;

	MD5((const unsigned char *)key, key_size, md5);
	ABT_mutex_lock(ring_list_mutex);
	for (i = 0; i < ring_list.n; ++i)
		if (memcmp(&ring_list.nodes[i].md5, md5, MD5_DIGEST_LENGTH)
		    >= 0)
			break;
	if (i == ring_list.n)
		i = 0;
	r = strdup(ring_list.nodes[i].address);
	ABT_mutex_unlock(ring_list_mutex);
	return (r);
}

static char *
ring_list_lookup_internal(uint8_t md5[], int low, int hi)
{
	int mid = (low + hi) / 2;

	if (hi - low == 1)
		return (strdup(ring_list.nodes[hi].address));
	if (memcmp(&ring_list.nodes[mid].md5, md5, MD5_DIGEST_LENGTH) < 0)
		return (ring_list_lookup_internal(md5, mid, hi));
	else
		return (ring_list_lookup_internal(md5, low, mid));
}

static char *
ring_list_lookup_binary(const char *key, int key_size)
{
	uint8_t md5[MD5_DIGEST_LENGTH];
	char *r;

	MD5((const unsigned char *)key, key_size, md5);
	ABT_mutex_lock(ring_list_mutex);
	if (memcmp(&ring_list.nodes[0].md5, md5, MD5_DIGEST_LENGTH) >= 0 ||
		memcmp(&ring_list.nodes[ring_list.n - 1].md5, md5,
			MD5_DIGEST_LENGTH) < 0) {
		r = strdup(ring_list.nodes[0].address);
	} else
		r = ring_list_lookup_internal(md5, 0, ring_list.n - 1);
	ABT_mutex_unlock(ring_list_mutex);
	return (r);
}

char *
ring_list_lookup(const char *key, int key_size)
{
	if (ring_list.n == 0)
		return (NULL);
	if (ring_list.n < 7)
		return (ring_list_lookup_linear(key, key_size));
	else
		return (ring_list_lookup_binary(key, key_size));
}

int
ring_list_is_coordinator(char *self)
{
	int i, ret = 0;

	ABT_mutex_lock(ring_list_mutex);
	for (i = 0; i < ring_list.n; ++i)
		if (strcmp(self, ring_list.nodes[i].address) < 0)
			break;
	if (i == ring_list.n)
		ret = 1;
	ABT_mutex_unlock(ring_list_mutex);
	return (ret);
}
