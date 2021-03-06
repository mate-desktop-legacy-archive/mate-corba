#include <config.h>
#include <stdio.h>
#include <matecorba/matecorba.h>

#include "matecorba-debug.h"
#include "orb-core-private.h"

static glong alive_root_objects = 0;
static glong total_refs = 0;


#ifdef G_ENABLE_DEBUG
static GHashTable *object_hash = NULL;

#define TYPE_CASE(e,n) \
	case MATECORBA_ROT_##e: str = g_strdup (n); break;

static void
object_hash_dump (gpointer key,
		  gpointer value,
		  gpointer user_data)
{
	char *str = NULL;
	MateCORBA_RootObject obj = key;
	const MateCORBA_RootObject_Interface *interface = obj->interface;
	
	switch (interface->type) {
		TYPE_CASE (NULL, "Null");
	case MATECORBA_ROT_OBJREF: {
		CORBA_Object o = (CORBA_Object) obj;
		str = g_strdup_printf ("Object (type '%s')",
				       g_quark_to_string (o->type_qid));
		break;
	}
	case MATECORBA_ROT_TYPECODE: {
		CORBA_TypeCode tc = (CORBA_TypeCode) obj;
		str = g_strdup_printf ("TypeCode (type '%s', kind %s)",
				       tc->repo_id,
				       MateCORBA_tk_to_name (tc->kind));
		break;
	}
		/* psuedo-objects */
		TYPE_CASE (ORB, "ORB");
		TYPE_CASE (ADAPTOR, "Adaptor");
		TYPE_CASE (POLICY, "Policy");
		TYPE_CASE (REQUEST, "Request");
		TYPE_CASE (SERVERREQUEST, "Server Request");
		TYPE_CASE (CONTEXT, "Context");
		TYPE_CASE (DYNANY, "Dynany");
		TYPE_CASE (OAOBJECT, "Object Adaptor Object");
		TYPE_CASE (ORBGROUP, "ORB Group");
		TYPE_CASE (POAMANAGER, "POA Manager");
		TYPE_CASE (POACURRENT, "Current POA");
		TYPE_CASE (CLIENT_POLICY, "Client policy");
	}

	if (!str)
		str = g_strdup_printf ("Error unknown type '%d'",
				       interface->type);

	fprintf (stderr, "%3d ref%c to '%s'\n",
		 obj->refs, obj->refs ? 's' : ' ', str);

	g_free (str);
}
#endif

int
MateCORBA_RootObject_shutdown (gboolean moan)
{
#ifdef G_ENABLE_DEBUG
	int valid_running = 1; /* The ORB */
#endif
	if (!moan)
		return 0;
#ifdef G_ENABLE_DEBUG
	if (!MateCORBA_RootObject_lifecycle_lock &&
	    alive_root_objects - valid_running)
		g_warning ("ORB: a total of %ld refs to %ld ORB "
			   "objects were leaked",
			   total_refs - valid_running,
			   alive_root_objects - valid_running);
	else if (total_refs - valid_running)
		g_warning ("ORB: a total of %ld refs to ORB "
			   "objects were leaked",
			   total_refs - valid_running);
	else
		return 0;

	if (_matecorba_debug_flags & MATECORBA_DEBUG_REFS)
		g_hash_table_foreach (
			object_hash, object_hash_dump, NULL);

	return 1;
#endif
	return 0;
}

void
MateCORBA_RootObject_init (MateCORBA_RootObject obj,
		       const MateCORBA_RootObject_Interface *interface)
{
	if (!MateCORBA_RootObject_lifecycle_lock) /* No locking */
		alive_root_objects++;

#ifdef G_ENABLE_DEBUG
	if (!object_hash)
		object_hash = g_hash_table_new (NULL, NULL);

	if (_matecorba_debug_flags & MATECORBA_DEBUG_REFS)
		g_hash_table_insert (object_hash, obj, obj);
#endif

	obj->interface = interface;
	obj->refs = 0;
}

gpointer
MateCORBA_RootObject_duplicate (gpointer obj)
{
	MateCORBA_RootObject robj = obj;

	if (robj && robj->refs != MATECORBA_REFCOUNT_STATIC) {
		LINK_MUTEX_LOCK   (MateCORBA_RootObject_lifecycle_lock);
		robj->refs++;
		total_refs++;
		LINK_MUTEX_UNLOCK (MateCORBA_RootObject_lifecycle_lock);
	}

	return obj;
}

gpointer
MateCORBA_RootObject_duplicate_T (gpointer obj)
{
	MateCORBA_RootObject robj = obj;

	if (robj && robj->refs != MATECORBA_REFCOUNT_STATIC) {
		robj->refs++;
		total_refs++;
	}

	return obj;
}

static void
do_unref (MateCORBA_RootObject robj)
{
	g_assert (robj->refs < MATECORBA_REFCOUNT_MAX && robj->refs > 0);

	robj->refs--;
	total_refs--;

	if (robj->refs == 0) {
		if (!MateCORBA_RootObject_lifecycle_lock) /* No locking */
			alive_root_objects--;
#ifdef G_ENABLE_DEBUG
		if (_matecorba_debug_flags & MATECORBA_DEBUG_REFS)
			g_hash_table_remove (object_hash, robj);
#endif

		if (robj->interface && robj->interface->destroy)
			robj->interface->destroy (robj);
		else
			g_free (robj);
	}
}

void
MateCORBA_RootObject_release_T (gpointer obj)
{
	MateCORBA_RootObject robj = obj;

	if (robj && robj->refs != MATECORBA_REFCOUNT_STATIC)
		do_unref (robj);
}

void
MateCORBA_RootObject_release (gpointer obj)
{
	MateCORBA_RootObject robj = obj;

	if (robj && robj->refs != MATECORBA_REFCOUNT_STATIC) {

		LINK_MUTEX_LOCK   (MateCORBA_RootObject_lifecycle_lock);

		do_unref (robj);

		LINK_MUTEX_UNLOCK (MateCORBA_RootObject_lifecycle_lock);
	}
}

