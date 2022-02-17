/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2022 Joyent, Inc.
 */

#include <libsysevent.h>
#include <sys/debug.h>
#include <sys/types.h>
#include <pthread.h>
#include <libnvpair.h>

#include "crossthread.h"
#include "illumos_list.h"

#include "more.h"

/*
 * C++ registers subscribing functions to be invoked in the event-loop thread,
 * and we track them in a list of "node_sysevent_t" objects:
 */
struct node_sysevent {
	nsev_callback_t *nse_func;
	void *nse_func_arg;
	list_node_t nse_node;
};

/*
 * Global state:
 */
sysevent_handle_t *g_nsev_handle;
list_t g_nsev_list;
static pthread_t g_nsev_loop_thread;
static int g_nsev_init_done = 0;


static int
nsev_in_loop_thread(void)
{
	return (g_nsev_loop_thread == pthread_self());
}

/*
 * This function executes on the eventloop thread via "crossthread_invoke()".
 */
static void
nsev_deliver(void *arg0, void *arg1)
{
	nvlist_t *nvl0 = arg0;
	nvlist_t *nvl1 = arg1;
	node_sysevent_t *nse;

	VERIFY(nsev_in_loop_thread());

	for (nse = list_head(&g_nsev_list); nse != NULL;
	    nse = list_next(&g_nsev_list, nse)) {
		nse->nse_func(nvl0, nvl1, nse->nse_func_arg);
	}
}

/*
 * This function executes in a delivery thread within the thread pool managed
 * by libsysevent.
 */
static void
nsev_handler(sysevent_t *ev)
{
	nvlist_t *nvl0;
	nvlist_t *nvl1 = NULL;
	pid_t evpid;

	VERIFY(!nsev_in_loop_thread());

	/*
	 * Construct an nvlist_t that describes the event.
	 */
	VERIFY0(nvlist_alloc(&nvl0, NV_UNIQUE_NAME, 0));
	VERIFY0(nvlist_add_string(nvl0, "class_name",
	    sysevent_get_class_name(ev)));
	VERIFY0(nvlist_add_string(nvl0, "subclass_name",
	    sysevent_get_subclass_name(ev)));
	VERIFY0(nvlist_add_string(nvl0, "vendor_name",
	    sysevent_get_vendor_name(ev)));
	VERIFY0(nvlist_add_string(nvl0, "publisher_name",
	    sysevent_get_pub_name(ev)));

	sysevent_get_pid(ev, &evpid);
	VERIFY0(nvlist_add_string(nvl0, "source",
	    (evpid == SE_KERN_PID) ? "kernel" : "user"));
	VERIFY0(nvlist_add_int32(nvl0, "pid", evpid));

	if (sysevent_get_attr_list(ev, &nvl1) != 0) {
		nvl1 = NULL;
	}

	VERIFY0(crossthread_invoke(nsev_deliver, nvl0, nvl1));

	nvlist_free(nvl0);
	nvlist_free(nvl1);
}

int
nsev_init(void)
{
	VERIFY(g_nsev_init_done == 0);
	g_nsev_init_done = 1;

	g_nsev_loop_thread = pthread_self();

	list_create(&g_nsev_list, sizeof (node_sysevent_t),
	    offsetof(node_sysevent_t, nse_node));

	return (0);
}

int
nsev_attach(nsev_callback_t *nsecb, void *arg, node_sysevent_t **nsep)
{
	node_sysevent_t *nse;

	VERIFY(nsev_in_loop_thread());

	*nsep = NULL;

	if ((nse = calloc(1, sizeof (*nse))) == NULL) {
		return (-1);
	}

	nse->nse_func = nsecb;
	nse->nse_func_arg = arg;

	if (list_is_empty(&g_nsev_list)) {
		const char *subclasses[] = {
			EC_SUB_ALL,
			NULL
		};
		VERIFY(g_nsev_handle == NULL);
		VERIFY((g_nsev_handle = sysevent_bind_handle(
		    nsev_handler)) != NULL);
		VERIFY0(sysevent_subscribe_event(g_nsev_handle,
		    EC_ALL, subclasses, 1));
	}
	list_insert_tail(&g_nsev_list, nse);

	*nsep = nse;
	return (0);
}

void
nsev_detach(node_sysevent_t *nse)
{
	VERIFY(nsev_in_loop_thread());

	if (nse == NULL) {
		return;
	}

	VERIFY(list_link_active(&nse->nse_node));
	list_remove(&g_nsev_list, nse);

	if (list_is_empty(&g_nsev_list)) {
		sysevent_unsubscribe_event(g_nsev_handle, EC_ALL);
		sysevent_unbind_handle(g_nsev_handle);
		g_nsev_handle = NULL;
	}

	free(nse);
}
