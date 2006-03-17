
/* Interface with openais's cman API */

#include "gd_internal.h"
#include "libcman.h"

static cman_handle_t	ch;
static int		old_quorate;
static cman_node_t	old_nodes[MAX_NODES];
static int		old_node_count;
static cman_node_t	cman_nodes[MAX_NODES];
static int		cman_node_count;
static int		cman_cb;
static int		cman_reason;


static int is_member(cman_node_t *node_list, int count, int nodeid)
{
	int i;

	for (i = 0; i < count; i++) {
		if (node_list[i].cn_nodeid == nodeid)
			return node_list[i].cn_member;
	}
	return 0;
}

static int is_old_member(int nodeid)
{
	return is_member(old_nodes, old_node_count, nodeid);
}

int is_cman_member(int nodeid)
{
	return is_member(cman_nodes, cman_node_count, nodeid);
}

static void statechange(void)
{
	int i, rv;

	old_quorate = cman_quorate;
	old_node_count = cman_node_count;
	memcpy(&old_nodes, &cman_nodes, sizeof(old_nodes));

	cman_quorate = cman_is_quorate(ch);

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	/*
	printf("cman: %d old nodes:\n", old_node_count);
	for (i = 0; i < old_node_count; i++)
		printf("%d:%d ", old_nodes[i].cn_nodeid,
				 old_nodes[i].cn_member);
	printf("\n");

	printf("cman: %d new nodes:\n", cman_node_count);
	for (i = 0; i < cman_node_count; i++)
		printf("%d:%d ", cman_nodes[i].cn_nodeid,
				 cman_nodes[i].cn_member);
	printf("\n");
	*/

	if (old_quorate && !cman_quorate)
		log_print("cman: lost quorum");
	if (!old_quorate && cman_quorate)
		log_print("cman: have quorum");

	for (i = 0; i < old_node_count; i++) {
		if (old_nodes[i].cn_member &&
		    !is_cman_member(old_nodes[i].cn_nodeid))
			log_print("cman: node %d removed",
				  old_nodes[i].cn_nodeid);
	}

	for (i = 0; i < cman_node_count; i++) {
		if (cman_nodes[i].cn_member &&
		    !is_old_member(cman_nodes[i].cn_nodeid))
			log_print("cman: node %d added",
				  cman_nodes[i].cn_nodeid);
	}
}

static void process_cman_callback(void)
{
	switch (cman_reason) {
	case CMAN_REASON_STATECHANGE:
		statechange();
		break;
	default:
		break;
	}
}

static void cman_callback(cman_handle_t h, void *private, int reason, int arg)
{
	cman_cb = 1;
	cman_reason = reason;

	if (reason == CMAN_REASON_TRY_SHUTDOWN) {
		if (list_empty(&gd_groups))
			cman_replyto_shutdown(ch, 1);
		else {
			log_debug("no to cman shutdown");
			cman_replyto_shutdown(ch, 0);
		}
	}
}

static void process_cman(int ci)
{
	int rv = 0;

	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);
		if (cman_cb) {
			cman_cb = 0;
			process_cman_callback();
			rv = 1;
		} else
			break;
	}
}

int setup_cman(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_print("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, cman_callback);
	if (rv < 0) {
		log_print("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_print("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		goto out;
	}

	cman_node_count = 0;
	memset(&cman_nodes, 0, sizeof(cman_nodes));
	rv = cman_get_nodes(ch, MAX_NODES, &cman_node_count, cman_nodes);
	if (rv < 0) {
		log_print("cman_get_nodes error %d %d", rv, errno);
		goto out;
	}

	cman_quorate = cman_is_quorate(ch);

	our_nodeid = node.cn_nodeid;
	log_debug("cman: our nodeid %d quorum %d", our_nodeid, cman_quorate);

	fd = cman_get_fd(ch);
	client_add(fd, process_cman);

	rv = 0;
 out:
	return rv;
}

