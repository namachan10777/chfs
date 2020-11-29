#include <margo.h>
#include "ring_types.h"
#include "ring_list.h"
#include "ring_list_rpc.h"
#include "log.h"

#define TIMEOUT_MSEC	(0)

static struct env {
	margo_instance_id mid;
	hg_id_t node_list_rpc;
} env;

static void node_list(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(node_list)

static hg_return_t
create_rpc_handle(const char *server, hg_id_t rpc_id, hg_handle_t *h)
{
	hg_addr_t addr;
	hg_return_t ret;

	ret = margo_addr_lookup(env.mid, server, &addr);
	if (ret != HG_SUCCESS)
		return (ret);
	ret = margo_create(env.mid, addr, rpc_id, h);
	margo_addr_free(env.mid, addr);
	return (ret);
}

hg_return_t
ring_list_rpc_node_list(const char *server)
{
	hg_handle_t h;
	hg_return_t ret, ret2;
	int32_t in = 0;
	string_list_t out;

	ret = create_rpc_handle(server, env.node_list_rpc, &h);
	if (ret != HG_SUCCESS)
		return (ret);

	ret = margo_forward_timed(h, &in, TIMEOUT_MSEC);
	if (ret != HG_SUCCESS)
		goto err;

	ret = margo_get_output(h, &out);
	if (ret != HG_SUCCESS)
		goto err;
	ring_list_update(&out);
	ret = margo_free_output(h, &out);
err:
	ret2 = margo_destroy(h);
	if (ret == HG_SUCCESS)
		ret = ret2;
	return (ret);
}

void
ring_list_rpc_init(margo_instance_id mid)
{
	env.mid = mid;
	env.node_list_rpc = MARGO_REGISTER(mid, "node_list", int32_t,
		string_list_t, node_list);
}

static void
node_list(hg_handle_t h)
{
	hg_return_t ret;
	int32_t in;
	string_list_t out;

	log_debug("node_list RPC");
	ret = margo_get_input(h, &in);
	assert(ret == HG_SUCCESS);
	ret = margo_free_input(h, &in);
	assert(ret == HG_SUCCESS);

	ring_list_copy(&out);
	ret = margo_respond(h, &out);
	assert(ret == HG_SUCCESS);
	ring_list_copy_free(&out);

	ret = margo_destroy(h);
	assert(ret == HG_SUCCESS);
}
DEFINE_MARGO_RPC_HANDLER(node_list)