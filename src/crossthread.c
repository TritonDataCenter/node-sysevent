
#include <stddef.h>
#include <strings.h>
#include <sys/debug.h>
#include <pthread.h>
#include <uv.h>

#include <node_version.h>

#include "crossthread.h"
#include "illumos_list.h"

#define	_UNUSED	__attribute__((__unused__))

typedef struct crossthread_call {
	pthread_mutex_t ctc_mtx;
	pthread_cond_t ctc_cv;

	list_node_t ctc_node;

	crossthread_func_t *ctc_func;
	void *ctc_arg0;
	void *ctc_arg1;

	int ctc_done;

} crossthread_call_t;


int g_crossthread_init_done;
pthread_mutexattr_t g_crossthread_mtxattr;
pthread_mutex_t g_crossthread_mtx;
uv_async_t g_crossthread_async;
list_t g_crossthread_queue;
static pthread_t g_crossthread_self;
static int g_crossthread_holds = 0;

int
crossthread_invoke(crossthread_func_t *func, void *arg0, void *arg1)
{
	crossthread_call_t ctc;

	VERIFY(pthread_self() != g_crossthread_self);

	/*
	 * Create a call tracking structure on the stack.
	 */
	bzero(&ctc, sizeof (ctc));
	VERIFY0(pthread_mutex_init(&ctc.ctc_mtx, &g_crossthread_mtxattr));
	VERIFY0(pthread_cond_init(&ctc.ctc_cv, NULL));
	ctc.ctc_func = func;
	ctc.ctc_arg0 = arg0;
	ctc.ctc_arg1 = arg1;

	/*
	 * Insert the struct in the call queue.
	 */
	VERIFY0(pthread_mutex_lock(&g_crossthread_mtx));
	list_insert_tail(&g_crossthread_queue, &ctc);
	VERIFY0(pthread_mutex_unlock(&g_crossthread_mtx));

	/*
	 * Schedule "crossthread_async_cb()" to run on the event loop thread.
	 */
	VERIFY0(uv_async_send(&g_crossthread_async));

	/*
	 * Wait for call to complete on event loop thread.
	 */
	VERIFY0(pthread_mutex_lock(&ctc.ctc_mtx));
	while (ctc.ctc_done == 0) {
		(void) pthread_cond_wait(&ctc.ctc_cv, &ctc.ctc_mtx);
	}
	VERIFY0(pthread_mutex_unlock(&ctc.ctc_mtx));

	VERIFY0(pthread_mutex_destroy(&ctc.ctc_mtx));
	VERIFY0(pthread_cond_destroy(&ctc.ctc_cv));
	return (0);
}

static void
#if NODE_VERSION_AT_LEAST(0, 11, 0)
crossthread_async_cb(uv_async_t *asy)
#else
crossthread_async_cb(uv_async_t *asy, int status _UNUSED)
#endif
{
	crossthread_call_t *ctc = NULL;

	VERIFY(pthread_self() == g_crossthread_self);

top:
	/*
	 * Attempt to get work from the queue:
	 */
	VERIFY0(pthread_mutex_lock(&g_crossthread_mtx));
	if (!list_is_empty(&g_crossthread_queue)) {
		ctc = list_remove_head(&g_crossthread_queue);
	}
	VERIFY0(pthread_mutex_unlock(&g_crossthread_mtx));

	if (ctc == NULL) {
		/*
		 * No work; back to sleep.
		 */
		return;
	}

	/*
	 * Ensure we haven't seen this one already:
	 */
	VERIFY0(pthread_mutex_lock(&ctc->ctc_mtx));
	VERIFY(ctc->ctc_done == 0);
	VERIFY0(pthread_mutex_unlock(&ctc->ctc_mtx));

	/*
	 * Run the enqueued function:
	 */
	ctc->ctc_func(ctc->ctc_arg0, ctc->ctc_arg1);

	/*
	 * Send reply back to waiting "crossthread_invoke()" call:
	 */
	VERIFY0(pthread_mutex_lock(&ctc->ctc_mtx));
	VERIFY(ctc->ctc_done == 0);
	ctc->ctc_done = 1;
	VERIFY0(pthread_cond_broadcast(&ctc->ctc_cv));
	VERIFY0(pthread_mutex_unlock(&ctc->ctc_mtx));

	ctc = NULL;
	goto top;
}

void
crossthread_take_hold(void)
{
	VERIFY(pthread_self() == g_crossthread_self);

	if (g_crossthread_holds++ == 0) {
		uv_ref((uv_handle_t *)&g_crossthread_async);
	}
}

void
crossthread_release_hold(void)
{
	VERIFY(pthread_self() == g_crossthread_self);
	VERIFY(g_crossthread_holds >= 1);

	if (--g_crossthread_holds == 0) {
		uv_unref((uv_handle_t *)&g_crossthread_async);
	}
}

int
crossthread_init(void)
{
	VERIFY(g_crossthread_init_done == 0);
	g_crossthread_init_done = 1;

	g_crossthread_self = pthread_self();

	list_create(&g_crossthread_queue, sizeof (crossthread_call_t),
	    offsetof(crossthread_call_t, ctc_node));

	VERIFY0(uv_async_init(uv_default_loop(), &g_crossthread_async,
	   crossthread_async_cb));

	VERIFY0(pthread_mutexattr_init(&g_crossthread_mtxattr));
	VERIFY0(pthread_mutexattr_settype(&g_crossthread_mtxattr,
	     PTHREAD_MUTEX_ERRORCHECK));

	VERIFY0(pthread_mutex_init(&g_crossthread_mtx, &g_crossthread_mtxattr));

	uv_unref((uv_handle_t *)&g_crossthread_async);

	return (0);
}
