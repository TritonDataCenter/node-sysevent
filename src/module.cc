#include <stdio.h>
#include <unistd.h>

#include <nan.h>
#include <sys/debug.h>
#include <thread.h>
#include <synch.h>
#include <libnvpair.h>

#include "more.h"
#include "crossthread.h"

using v8::Local;
using v8::Object;
using v8::Handle;
using v8::Value;
using v8::External;
using v8::FunctionTemplate;
using v8::Function;

/*
 * This struct is used to track the C++ state of the native part of this module:
 */
typedef struct node_sysevent_cpp {
	/*
	 * A persistent reference to the callback function to call when
	 * delivering sysevent notifications to Javascript:
	 */
	Nan::Callback *nsec_func;

	/*
	 * A persistent reference to the JS object created from our C++
	 * function template:
	 */
	Nan::Global<Object> *nsec_obj;

	/*
	 * A handle to the C routines in "more.c":
	 */
	node_sysevent_t *nsec_hdl;

	/*
	 * 0 until ".destroy()" called; 1 afterward:
	 */
	int nsec_destroyed;

} node_sysevent_cpp_t;

/*
 * On object "self", set the internal field slot "idx" to the pointer value
 * "data".  The pointer will be encoded as a "v8::External" value.
 */
void
set_internal_pointer(Local<Object> self, int idx, void *data)
{
	Local<External> ext = Nan::New<External>(data);

	self->SetInternalField(idx, ext);
}

/*
 * Get the value of internal field slot "idx" on object "self" and return it as
 * a (void *).  The pointer must have been encoded as a "v8::External" value.
 */
void *
get_internal_pointer(Local<Object> self, int idx)
{
	Local<External> ext = self->GetInternalField(idx).As<External>();
	void *vp = reinterpret_cast<void *>(ext->Value());

	return (vp);
}

/*
 * Attach contents of an nvlist_t "nvl" to the JS object "obj":
 */
int
node_sysevent_nvlist_to_object(nvlist_t *nvl, Local<Object> obj)
{
	nvpair_t *nvp = NULL;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		switch (nvpair_type(nvp)) {
		case DATA_TYPE_STRING: {
			char *val;

			VERIFY0(nvpair_value_string(nvp, &val));

			Nan::Set(obj,
			    Nan::New(nvpair_name(nvp)).ToLocalChecked(),
			    Nan::New(val).ToLocalChecked());
			break;
	       }

		case DATA_TYPE_INT32: {
			int32_t val;

			VERIFY0(nvpair_value_int32(nvp, &val));

			Nan::Set(obj,
			    Nan::New(nvpair_name(nvp)).ToLocalChecked(),
			    Nan::New(val));
			break;
		}

		default:
			fprintf(stderr, "unknown type: %d\n", nvpair_type(nvp));
			break;
		}
	}

	return (0);
}

/*
 * This callback (with C calling convention) is passed to the C side of the
 * implementation.  It will be called when we receive notification of a
 * sysevent.  See "more.c" for further information.
 */
extern "C" void
node_sysevent_deliver(nvlist_t *nvl0, nvlist_t *nvl1, void *arg)
{
	node_sysevent_cpp_t *nsec = (node_sysevent_cpp_t *)arg;

	/*
	 * Arguments to the callback:
	 */
	Local<Object> obj0 = Nan::New<Object>();
	Local<Object> obj1 = Nan::New<Object>();
	Local<Value> argv[] = { obj0, obj1 };

	if (nvl0 != NULL) {
		VERIFY0(node_sysevent_nvlist_to_object(nvl0, obj0));
	}
	if (nvl1 != NULL) {
		VERIFY0(node_sysevent_nvlist_to_object(nvl1, obj1));
	}

	nsec->nsec_func->Call(2, argv);
}

/*
 * The finaliser for the JS object created from our C++ function template.
 * Only called once "node_sysevent_destroy_common()" has made our
 * self-reference (nsec_obj) weak, _and_ the object is to be garbage collected.
 */
void
node_sysevent_dtor(const Nan::WeakCallbackInfo<node_sysevent_cpp_t> &data)
{
	node_sysevent_cpp_t *nsec = data.GetParameter();

	/*
	 * We should not get here until ".destroy()" has been called.
	 */
	VERIFY(nsec->nsec_destroyed != 0);

	/*
	 * Now that our weak callback has fired, delete the weak reference
	 * and free our tracking structure.
	 */
	delete nsec->nsec_obj;
	free(nsec);
}

/*
 * Tear down any resources we allocated, except for the base tracking structure.
 * This routine is used to implement ".destroy()".
 */
void
node_sysevent_destroy_common(node_sysevent_cpp_t *nsec)
{
	if (nsec->nsec_destroyed) {
		return;
	}
	nsec->nsec_destroyed = 1;

	if (nsec->nsec_hdl != NULL) {
		/*
		 * Detach from the subscription first to ensure no further
		 * calls to node_sysevent_deliver().
		 */
		nsev_detach(nsec->nsec_hdl);
		nsec->nsec_hdl = NULL;

		/*
		 * Stop holding the event loop open.
		 */
		crossthread_release_hold();
	}

	/*
	 * Remove reference to our event delivery callback:
	 */
	if (nsec->nsec_func != NULL) {
		delete nsec->nsec_func;
		nsec->nsec_func = NULL;
	}

	/*
	 * Make our persistent reference to our own object weak, so that it
	 * can be garbage collected.
	 */
	nsec->nsec_obj->SetWeak(nsec, node_sysevent_dtor,
	    Nan::WeakCallbackType::kParameter);
}

/*
 * This constructor is called for each invocation of "SyseventImpl()", with or
 * without the "new" operator.
 */
static
NAN_METHOD(node_sysevent_ctor)
{
	Local<Object> self = info.This();
	node_sysevent_cpp_t *nsec;

	/*
	 * We don't expose this class to consumers directly, so just make sure
	 * we're doing the right thing with respect to "new" and provided
	 * arguments, etc.
	 */
	if (!info.IsConstructCall() || info.Length() != 1) {
		Nan::ThrowError("invalid constructor call");
		return;
	}

	/*
	 * Allocate our tracking structure and set our first internal field
	 * slot to point to it.
	 */
	if ((nsec = (node_sysevent_cpp_t *)calloc(1, sizeof (*nsec))) == NULL) {
		Nan::ThrowError("could not allocate tracking struct");
		return;
	}
	set_internal_pointer(self, 0, (void *)nsec);

	/*
	 * Create a persistent reference to ourselves, so that we are not
	 * garbage collected until ".destroy()" has been called.
	 */
	nsec->nsec_obj = new Nan::Global<Object>(self);

	/*
	 * Create a persistent reference to the callback function we were
	 * passed, so that we may call it asynchronously.
	 */
	nsec->nsec_func = new Nan::Callback(info[0].As<Function>());

	/*
	 * Attach to the sysevent subscription.
	 */
	if (nsev_attach(node_sysevent_deliver, (void *)nsec,
	    &nsec->nsec_hdl) != 0) {
		Nan::ThrowError("could not connect to sysevent");
		return;
	}

	/*
	 * Hold the event loop open while we wait for sysevents.  The consumer
	 * must call ".destroy()" (or this object must be collected) to prevent
	 * us holding open the event loop forever.
	 */
	crossthread_take_hold();
}

/*
 * The ".destroy()" method on the JS object.
 */
static
NAN_METHOD(node_sysevent_destroy)
{
	Local<Object> self = info.This();
	node_sysevent_cpp_t *nsec = (node_sysevent_cpp_t *)
	    get_internal_pointer(self, 0);

	node_sysevent_destroy_common(nsec);
}

/*
 * Create the function template for the "SyseventImpl" Javascript class and
 * export it.
 */
static void
node_sysevent_init(Handle<Object> exports)
{
	Local<FunctionTemplate> t = Nan::New<FunctionTemplate>(node_sysevent_ctor);

	t->SetClassName(Nan::New("SyseventImpl").ToLocalChecked());
	t->InstanceTemplate()->SetInternalFieldCount(1);

	Nan::SetPrototypeMethod(t, "destroy", node_sysevent_destroy);

	exports->Set(Nan::New("SyseventImpl").ToLocalChecked(),
	    t->GetFunction());
}

NAN_MODULE_INIT(module_init)
{
	/*
	 * Create our global sysevent subscription.  We will use this
	 * subscription to underpin all Javascript-level subscription
	 * objects.
	 */
	if (nsev_init() != 0) {
		Nan::ThrowError("could not init sysevent handler");
	}

	VERIFY0(crossthread_init());

	node_sysevent_init(target);
}

NODE_MODULE(module, module_init)
