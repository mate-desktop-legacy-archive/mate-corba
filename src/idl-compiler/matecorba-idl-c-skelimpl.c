#include "config.h"

#include "matecorba-idl-c-backend.h"

/* This file copied from the old IDL compiler matecorba-c-skelimpl.c, with minimal changes. */

static void matecorba_cbe_write_skelimpl(FILE *outfile, IDL_tree tree, const char *hdrname);

void
matecorba_idl_output_c_skelimpl(IDL_tree tree, OIDL_Run_Info *rinfo, OIDL_C_Info *ci)
{
	matecorba_cbe_write_skelimpl(ci->fh, tree, ci->base_name);
}

#include <ctype.h>
#include <errno.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Abbreviations used here:
   "cbe" stands for "C backend"
   "hdr" -> "header" (duh :)
   "of" -> "output file"
   "ns" -> "name space"
*/

typedef struct {
	FILE *of;
	IDL_tree tree;
	enum { PASS_SERVANTS, PASS_PROTOS, PASS_EPVS, PASS_VEPVS,
	       PASS_IMPLSTUBS, PASS_LAST } pass;
} CBESkelImplInfo;

static const char *passnames[] = {
	"App-specific servant structures",
	"Implementation stub prototypes",
	"epv structures",
	"vepv structures",
	"Stub implementations",
	"Boohoo!"
};

static void matecorba_cbe_ski_process_piece(CBESkelImplInfo *ski);
static void cbe_ski_do_list(CBESkelImplInfo *ski);
static void cbe_ski_do_inherited_attr_dcl(CBESkelImplInfo *ski, IDL_tree current_interface);
static void cbe_ski_do_attr_dcl(CBESkelImplInfo *ski);
static void cbe_ski_do_inherited_op_dcl(CBESkelImplInfo *ski, IDL_tree current_interface);
static void cbe_ski_do_op_dcl(CBESkelImplInfo *ski);
static void cbe_ski_do_param_dcl(CBESkelImplInfo *ski);
static void cbe_ski_do_interface(CBESkelImplInfo *ski);
static void cbe_ski_do_module(CBESkelImplInfo *ski);

static void
matecorba_cbe_write_skelimpl(FILE *outfile, IDL_tree tree, const char *hdrname)
{
	CBESkelImplInfo ski = {NULL, NULL, PASS_SERVANTS};

	ski.of = outfile;
	ski.tree = tree;

	g_return_if_fail(IDL_NODE_TYPE(tree) == IDLN_LIST);

	fprintf(outfile, "/* This is a template file generated by command */\n");
	fprintf(outfile, "/* matecorba-idl-2 --skeleton-impl %s.idl */\n", hdrname);
	fprintf(outfile, "/* User must edit this file, inserting servant  */\n");
	fprintf(outfile, "/* specific code between markers. */\n\n");

	fprintf(outfile, "#include \"%s.h\"\n", hdrname);

	for(ski.pass = PASS_SERVANTS; ski.pass < PASS_LAST; ski.pass++) {
		fprintf(ski.of, "\n/*** %s ***/\n\n", passnames[ski.pass]);
		matecorba_cbe_ski_process_piece(&ski);
	}
}

static void
matecorba_cbe_ski_process_piece(CBESkelImplInfo *ski)
{
	/* I'm not implementing this as an array of function pointers
	   because we may want to do special logic for particular cases in
	   the future. Hope this is clear enough. -ECL */

	switch(IDL_NODE_TYPE(ski->tree)) {
	case IDLN_ATTR_DCL:
		cbe_ski_do_attr_dcl(ski);
		break;
	case IDLN_INTERFACE:
		cbe_ski_do_interface(ski);
		break;
	case IDLN_LIST:
		cbe_ski_do_list(ski);
		break;
	case IDLN_MODULE:
		cbe_ski_do_module(ski);
		break;
	case IDLN_OP_DCL:
		cbe_ski_do_op_dcl(ski);
		break;
	case IDLN_PARAM_DCL:
		cbe_ski_do_param_dcl(ski);
		break;
	default:
		break;
	}
}

static void
cbe_ski_do_module(CBESkelImplInfo *ski)
{
	CBESkelImplInfo subski = *ski;
	subski.tree = IDL_MODULE(ski->tree).definition_list;
	cbe_ski_do_list(&subski);
}

/* Returns 1 if the previous character written to f  */
/* was '\n', 0 otherwise. */
static inline unsigned char 
prev_char_is_nl(FILE *f)
{
        char c;
	long pos;
	size_t count;
        unsigned char retv = 0;

	pos = ftell(f);
	if (pos < sizeof(char)) 
		return 0; /* beginning of file */

        if (fseek(f, (-1)*sizeof(char), SEEK_CUR)) 
		goto out;

	count = fread((void*)&c, sizeof(char), 1, f);
        if (sizeof(char) == count) 
		retv = ('\n' == c) ? 1 : 0;
	
out:
	fseek(f, pos, SEEK_SET);
        return retv;
}

static void
cbe_ski_do_list(CBESkelImplInfo *ski)
{
	CBESkelImplInfo subski = *ski;
	IDL_tree curitem;

	for(curitem = ski->tree; curitem; curitem = IDL_LIST(curitem).next) {
		subski.tree = IDL_LIST(curitem).data;
		matecorba_cbe_ski_process_piece(&subski);
		if(!prev_char_is_nl(ski->of))
			fprintf(ski->of, "\n");
	}
}

static void 
cbe_ski_do_attr_dcl_internal(CBESkelImplInfo *ski, IDL_tree current_interface, gboolean inherited)
{
	IDL_tree curop, curitem;
	GString *attrname = g_string_new(NULL);
	CBESkelImplInfo subski = *ski;

	if(ski->pass == PASS_SERVANTS) {
		for(curitem = IDL_ATTR_DCL(ski->tree).simple_declarations; curitem;
		    curitem = IDL_LIST(curitem).next) {
			matecorba_cbe_write_typespec(ski->of, IDL_ATTR_DCL(ski->tree).param_type_spec);
			fprintf(ski->of, " attr_%s;\n", IDL_IDENT(IDL_LIST(curitem).data).str);
		}
	}

	for(curitem = IDL_ATTR_DCL(ski->tree).simple_declarations;
	    curitem; curitem = IDL_LIST(curitem).next) {

		/* Fake the attribute get/set methods as operation declarations */
		IDL_tree ident, ns_data_save;
		int i;

		for (i = 0; i < 2; ++i) {

			if (i && IDL_ATTR_DCL(ski->tree).f_readonly)
				break;
			/* Output the operation on this attribute */
			g_string_printf(attrname, i ? "_set_%s" : "_get_%s",
					IDL_IDENT(IDL_LIST(curitem).data).str);
			ident = IDL_ident_new(g_strdup(attrname->str));
	    
			/* Tell the ident where our namespace node is, and request a return value
			   if this is the _get operation */
			IDL_IDENT_TO_NS(ident) = IDL_IDENT_TO_NS(IDL_LIST(curitem).data);
			curop = IDL_op_dcl_new(0, i == 0 ?
					       IDL_ATTR_DCL(ski->tree).param_type_spec : NULL,
					       ident, NULL, NULL, NULL);
	    
			curop->up = ski->tree->up;
			subski.tree = curop;
	    
			/* Save the namespace ident (IDL_GENTREE data) reference, assign
			   back to the temporary tree, output the operation, then restore
			   the namespace ident link */
			ns_data_save = IDL_GENTREE(IDL_IDENT_TO_NS(IDL_LIST(curitem).data)).data;
			IDL_GENTREE(IDL_IDENT_TO_NS(IDL_LIST(curitem).data)).data = ident;

			if (i) {
				/* The set routine also needs the value, so we
				   temporarily add that to the operation
				   declaration */
				IDL_OP_DCL(curop).parameter_dcls = IDL_list_new(
					IDL_param_dcl_new(IDL_PARAM_IN,
							  IDL_ATTR_DCL(ski->tree).param_type_spec,
							  IDL_ident_new(g_strdup("value"))));
			}
	    
			if(inherited==TRUE)
				cbe_ski_do_inherited_op_dcl(&subski, current_interface);
			else
				matecorba_cbe_ski_process_piece(&subski);

			/* Restore the fake link to the original in the namespace */
			IDL_GENTREE(IDL_IDENT_TO_NS(IDL_LIST(curitem).data)).data = ns_data_save;

			if (i) {
				/* Free only what we've created for the fake node, so remove 
				   the attribute node element and then free the rest */
				IDL_PARAM_DCL(IDL_LIST(
						      IDL_OP_DCL(curop).parameter_dcls).data).param_type_spec = NULL;
			}
	    
			/* Remove what we've "borrowed" from ATTR_DCL from the
			   fake curop node then free the rest */
			IDL_OP_DCL(curop).op_type_spec = NULL;
			IDL_tree_free(curop);
		}
	}

	g_string_free(attrname, TRUE);
}

static void 
cbe_ski_do_attr_dcl(CBESkelImplInfo *ski)
{
	cbe_ski_do_attr_dcl_internal(ski, NULL, FALSE);
}

void
cbe_ski_do_inherited_attr_dcl(CBESkelImplInfo *ski, IDL_tree current_interface)
{
	cbe_ski_do_attr_dcl_internal(ski, current_interface, TRUE);
}

static void
cbe_ski_do_op_dcl(CBESkelImplInfo *ski)
{
	/* If you fix anything here, please also fix it in
	   cbe_ski_do_inherited_op_dcl(), which is almost a
	   cut-and-paste of this routine */

	char *id, *id2;
	IDL_tree curitem, op;
	int level;
	CBESkelImplInfo subski = *ski;

	switch(ski->pass) {
	case PASS_PROTOS:
	case PASS_IMPLSTUBS:
		curitem = IDL_get_parent_node(ski->tree, IDLN_INTERFACE, &level);
		g_assert(curitem);

		id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_OP_DCL(ski->tree).ident), "_", 0);    
		id2 = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(curitem).ident), "_", 0);

		/* protect with #ifdef block  */
		if(PASS_PROTOS == ski->pass) 
			fprintf(ski->of, "#if !defined(_decl_impl_");
		else
			fprintf(ski->of, "#if !defined(_impl_");
		fprintf(ski->of, "%s_)\n", id);

		if(PASS_PROTOS == ski->pass) 
			fprintf(ski->of, "#define _decl_impl_");
		else
			fprintf(ski->of, "#define _impl_");
		fprintf(ski->of, "%s_ 1\n", id);

		fprintf(ski->of, "static ");
		matecorba_cbe_write_param_typespec(ski->of, ski->tree);    
		fprintf(ski->of, "\nimpl_%s(impl_POA_%s *servant,\n", id, id2);
		g_free(id); g_free(id2);
    
		op = ski->tree;
		for(curitem = IDL_OP_DCL(ski->tree).parameter_dcls;
		    curitem; curitem = IDL_LIST(curitem).next) {
			subski.tree = IDL_LIST(curitem).data;
			matecorba_cbe_ski_process_piece(&subski);
		}

		if(IDL_OP_DCL(op).context_expr)
			fprintf(ski->of, "CORBA_Context ctx,\n");

		fprintf(ski->of, "CORBA_Environment *ev)");
		if(ski->pass == PASS_IMPLSTUBS) {
			fprintf(ski->of, "\n{\n");
			if(IDL_OP_DCL(op).op_type_spec) {
				matecorba_cbe_write_param_typespec(ski->of, ski->tree);
				fprintf(ski->of, " retval;\n");
				fprintf(ski->of, " /* ------   insert method code here   ------ */\n");
				fprintf(ski->of, " /* ------ ---------- end ------------ ------ */\n");
				fprintf(ski->of, "\nreturn retval;\n");
			}
			else
			{	
				fprintf(ski->of, " /* ------   insert method code here   ------ */\n");
				fprintf(ski->of, " /* ------ ---------- end ------------ ------ */\n");
			}
			fprintf(ski->of, "}\n");
		} else /* PASS_PROTOS */
			fprintf(ski->of, ";\n");

		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */
		break; /* End PASS_PROTOS | PASS_IMPLSTUBS */
	case PASS_EPVS:
		id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_OP_DCL(ski->tree).ident), "_", 0);
		fprintf(ski->of, "(gpointer)&impl_%s,\n", id);
		g_free(id);
		break;
	default:
		break;
	}
}

static void
cbe_ski_do_inherited_op_dcl(CBESkelImplInfo *ski, IDL_tree current_interface)
{
	char *id, *id2;
	IDL_tree ident, curitem, intf, op;
	int level;
	CBESkelImplInfo subski = *ski;

	id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(current_interface).ident), "_", 0);
	intf = IDL_get_parent_node(ski->tree, IDLN_INTERFACE, NULL);
	id2 = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(intf).ident), "_", 0);

	ident=IDL_OP_DCL(ski->tree).ident;
	g_assert(ident);

	switch(ski->pass) {
	case PASS_PROTOS:
	case PASS_IMPLSTUBS:
		curitem = IDL_get_parent_node(ski->tree, IDLN_INTERFACE, &level);
		g_assert(curitem);

		/* protect with #ifdef block  */
		if(PASS_PROTOS == ski->pass) 
			fprintf(ski->of, "#if !defined(_decl_impl_");
		else
			fprintf(ski->of, "#if !defined(_impl_");
		fprintf(ski->of, "%s_%s_)\n", id, IDL_IDENT(ident).str);

		if(PASS_PROTOS == ski->pass) 
			fprintf(ski->of, "#define _decl_impl_");
		else
			fprintf(ski->of, "#define _impl_");
		fprintf(ski->of, "%s_%s_ 1\n", id, IDL_IDENT(ident).str);

		fprintf(ski->of, "static ");
		matecorba_cbe_write_param_typespec(ski->of, ski->tree);
    		fprintf(ski->of, "\nimpl_%s_%s(impl_POA_%s *servant,\n", id, IDL_IDENT(ident).str, id);
    
		op = ski->tree;
		for(curitem = IDL_OP_DCL(ski->tree).parameter_dcls;
		    curitem; curitem = IDL_LIST(curitem).next) {
			subski.tree = IDL_LIST(curitem).data;
			matecorba_cbe_ski_process_piece(&subski);
		}

		if(IDL_OP_DCL(op).context_expr)
			fprintf(ski->of, "CORBA_Context ctx,\n");

		fprintf(ski->of, "CORBA_Environment *ev)");
		if(ski->pass == PASS_IMPLSTUBS) {
			fprintf(ski->of, "\n{\n");
			if(IDL_OP_DCL(op).op_type_spec) {
				matecorba_cbe_write_param_typespec(ski->of, ski->tree);
				fprintf(ski->of, " retval;\n");
				fprintf(ski->of, " /* ------   insert method code here   ------ */\n");
				fprintf(ski->of, " /* ------ ---------- end ------------ ------ */\n");
				fprintf(ski->of, "\nreturn retval;\n");
			}
			else
			{	
				fprintf(ski->of, " /* ------   insert method code here   ------ */\n");
				fprintf(ski->of, " /* ------ ---------- end ------------ ------ */\n");
			}
			fprintf(ski->of, "}\n");
		} else /* PASS_PROTOS */
			fprintf(ski->of, ";\n");

		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */
		break; /* End PASS_PROTOS | PASS_IMPLSTUBS */
	case PASS_EPVS:
		ident=IDL_OP_DCL(ski->tree).ident;
		g_assert(ident);

		fprintf(ski->of, "(gpointer)&impl_%s_%s,\n", id, IDL_IDENT(ident).str);
	default:
		break;
	}

	g_free(id);
	g_free(id2);
}

static void
cbe_ski_do_param_dcl(CBESkelImplInfo *ski)
{
	matecorba_cbe_write_param_typespec(ski->of, ski->tree);
	fprintf(ski->of, " %s,\n", IDL_IDENT(IDL_PARAM_DCL(ski->tree).simple_declarator).str);
}

static void
cbe_ski_do_interface_vepv_entry(IDL_tree interface, CBESkelImplInfo *ski)
{
	char *id, *inherit_id;

	if(interface==ski->tree) {
		id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(ski->tree).ident), "_", 0);
		fprintf(ski->of, "&impl_%s_epv,\n", id);
		g_free(id);
		return;
	}

	id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(ski->tree).ident), "_", 0);
	inherit_id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(interface).ident),
					     "_", 0);
	fprintf(ski->of, "&impl_%s_%s_epv,\n", id, inherit_id);

	g_free(id);
	g_free(inherit_id);
}

static void
cbe_ski_do_inherited_methods(IDL_tree interface, CBESkelImplInfo *ski)
{
	CBESkelImplInfo subski= *ski;
	IDL_tree curitem;
	char *id = NULL, *inherit_id = NULL; /* Quiet gcc */

	if(interface==ski->tree)
		return;

	if(ski->pass==PASS_EPVS) {
		id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(ski->tree).ident),
					     "_", 0);
		inherit_id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(interface).ident),
						     "_", 0);
		/* protect with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s_%s_epv_)\n", id, inherit_id);
		fprintf(ski->of, "#define _impl_%s_%s_epv_ 1\n", id, inherit_id);
		fprintf(ski->of, "static POA_%s__epv impl_%s_%s_epv = {\nNULL, /* _private */\n", inherit_id, id, inherit_id);
	}

	for(curitem = IDL_INTERFACE(interface).body; curitem; curitem=IDL_LIST(curitem).next) {
		subski.tree=IDL_LIST(curitem).data;

		switch(IDL_NODE_TYPE(IDL_LIST(curitem).data)) {
		case IDLN_OP_DCL:
			cbe_ski_do_inherited_op_dcl(&subski, ski->tree);
			break;
		case IDLN_ATTR_DCL:
			cbe_ski_do_inherited_attr_dcl(&subski, ski->tree);
			break;
		default:
			break;
		}
	}

	if(ski->pass==PASS_EPVS) {
		fprintf(ski->of, "};\n"); 
		fprintf(ski->of, "#endif\n"); /* end of protective #ifdef block */

		g_free(id);
		g_free(inherit_id);
	}
}

static void
cbe_ski_do_interface(CBESkelImplInfo *ski)
{
	char *id;
	CBESkelImplInfo subski = *ski;

	id = IDL_ns_ident_to_qstring(IDL_IDENT_TO_NS(IDL_INTERFACE(ski->tree).ident), "_", 0);

	switch(ski->pass) {
	case PASS_SERVANTS:
		/* protect with #ifdef block  */
		fprintf(ski->of, "#if !defined(_typedef_impl_POA_%s_)\n", id);
		fprintf(ski->of, "#define _typedef_impl_POA_%s_ 1\n", id);

		fprintf(ski->of, "typedef struct {\nPOA_%s servant;\nPortableServer_POA poa;\n", id);
		subski.tree = IDL_INTERFACE(ski->tree).body;
		cbe_ski_do_list(&subski);
		IDL_tree_traverse_parents(ski->tree, (GFunc)&cbe_ski_do_inherited_methods, ski);
		fprintf(ski->of, "   /* ------ add private attributes here ------ */\n");
		fprintf(ski->of, "   /* ------ ---------- end ------------ ------ */\n");
		fprintf(ski->of, "} impl_POA_%s;\n", id);

		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */
		break;
	case PASS_EPVS:
		/* protect with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s_base_epv_)\n", id);
		fprintf(ski->of, "#define _impl_%s_base_epv_ 1\n", id);
		fprintf(ski->of, "static PortableServer_ServantBase__epv impl_%s_base_epv = {\n", id);
		fprintf(ski->of, "NULL,             /* _private data */\n");
		fprintf(ski->of, "(gpointer) & impl_%s__destroy, /* finalize routine */\n", id);
		fprintf(ski->of, "NULL,             /* default_POA routine */\n");
		fprintf(ski->of, "};\n");
		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */

		/* protect with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s_epv_)\n", id);
		fprintf(ski->of, "#define _impl_%s_epv_ 1\n", id);
		fprintf(ski->of, "static POA_%s__epv impl_%s_epv = {\nNULL, /* _private */\n", id, id);
		subski.tree = IDL_INTERFACE(ski->tree).body;
		cbe_ski_do_list(&subski);
		fprintf(ski->of, "};\n");
		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */

		IDL_tree_traverse_parents(ski->tree, (GFunc)&cbe_ski_do_inherited_methods, ski);
		break;
	case PASS_VEPVS:
		/* protect with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s_vepv_)\n", id);
		fprintf(ski->of, "#define _impl_%s_vepv_ 1\n", id);

		fprintf(ski->of, "static POA_%s__vepv impl_%s_vepv = {\n", id, id);
		fprintf(ski->of, "&impl_%s_base_epv,\n", id);
		IDL_tree_traverse_parents(ski->tree, (GFunc)&cbe_ski_do_interface_vepv_entry, ski);
		fprintf(ski->of, "};\n");

		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */
		break;
	case PASS_IMPLSTUBS:
		/* protect __create with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s__create_)\n", id);
		fprintf(ski->of, "#define _impl_%s__create_ 1\n", id);
		fprintf(ski->of, "static %s impl_%s__create(PortableServer_POA poa, CORBA_Environment *ev)\n", id, id);
		fprintf(ski->of, "{\n%s retval;\nimpl_POA_%s *newservant;\nPortableServer_ObjectId *objid;\n\n", id, id);
		fprintf(ski->of, "newservant = g_new0(impl_POA_%s, 1);\n", id);
		fprintf(ski->of, "newservant->servant.vepv = &impl_%s_vepv;\n", id);
		fprintf(ski->of, "newservant->poa = (PortableServer_POA) CORBA_Object_duplicate((CORBA_Object)poa, ev);\n");
		fprintf(ski->of, "POA_%s__init((PortableServer_Servant)newservant, ev);\n", id);
    		fprintf(ski->of, "   /* Before servant is going to be activated all\n");
		fprintf(ski->of, "    * private attributes must be initialized.  */\n"); 
		fprintf(ski->of, "\n");
		fprintf(ski->of, "   /* ------ init private attributes here ------ */\n");
		fprintf(ski->of, "   /* ------ ---------- end ------------- ------ */\n");
		fprintf(ski->of, "\n");
		fprintf(ski->of, "objid = PortableServer_POA_activate_object(poa, newservant, ev);\n");
		fprintf(ski->of, "CORBA_free(objid);\n");
		fprintf(ski->of, "retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);\n");
		fprintf(ski->of, "\nreturn retval;\n}\n");
		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */

		/* protect __destroy with #ifdef block  */
		fprintf(ski->of, "#if !defined(_impl_%s__destroy_)\n", id);
		fprintf(ski->of, "#define _impl_%s__destroy_ 1\n", id);
		fprintf(ski->of, "static void\nimpl_%s__destroy(impl_POA_%s *servant, CORBA_Environment *ev)\n{\n", id, id);
		fprintf(ski->of, "    CORBA_Object_release ((CORBA_Object) servant->poa, ev);\n\n");
		fprintf(ski->of, "    /* No further remote method calls are delegated to \n");
		fprintf(ski->of, "    * servant and you may free your private attributes. */\n");
		fprintf(ski->of, "   /* ------ free private attributes here ------ */\n");
		fprintf(ski->of, "   /* ------ ---------- end ------------- ------ */\n");
		fprintf(ski->of, "\nPOA_%s__fini((PortableServer_Servant)servant, ev);\n", id);
		fprintf(ski->of, "\ng_free (servant);\n");
		fprintf(ski->of, "}\n");
		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */

		subski.tree = IDL_INTERFACE(ski->tree).body;
		cbe_ski_do_list(&subski);
		IDL_tree_traverse_parents(ski->tree, (GFunc)&cbe_ski_do_inherited_methods, ski);
		break;
	case PASS_PROTOS:
		/* protect __destroy declaration with #ifdef block  */
		fprintf(ski->of, "#if !defined(_decl_impl_%s__destroy_)\n", id);
		fprintf(ski->of, "#define _decl_impl_%s__destroy_ 1\n", id);
		fprintf(ski->of, "static void impl_%s__destroy(impl_POA_%s *servant,\nCORBA_Environment *ev);\n", id, id);
		fprintf(ski->of, "#endif\n\n"); /* end of protective #ifdef block */

		subski.tree = IDL_INTERFACE(ski->tree).body;
		cbe_ski_do_list(&subski);
		IDL_tree_traverse_parents(ski->tree, (GFunc)&cbe_ski_do_inherited_methods, ski);
		break;
	default:
		break;
	}

	g_free(id);
}
