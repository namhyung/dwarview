#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "dwarview.h"

/* Dwarf FL wrappers */
static char *debuginfo_path;	/* Currently dummy */

static const Dwfl_Callbacks offline_callbacks = {
	.find_debuginfo = dwfl_standard_find_debuginfo,
	.debuginfo_path = &debuginfo_path,

	.section_address = dwfl_offline_section_address,

	/* We use this table for core files too.  */
	.find_elf = dwfl_build_id_find_elf,
};

static Dwarf *dwarf;

/* Get a Dwarf from offline image */
static int open_dwarf_file(char *path)
{
	int fd;
	int err;
	Dwfl *dwfl;
	Dwfl_Module *mod;
	Dwarf_Addr bias;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	dwfl = dwfl_begin(&offline_callbacks);
	if (!dwfl)
		goto error;

	dwfl_report_begin(dwfl);
	mod = dwfl_report_offline(dwfl, "", "", fd);
	if (!mod)
		goto error;

	dwarf = dwfl_module_getdwarf(mod, &bias);
	if (!dwarf)
		goto error;

	dwfl_report_end(dwfl, NULL, NULL);

	return 0;

error:
	err = dwarf_errno();

	if (dwfl)
		dwfl_end(dwfl);
	else
		close(fd);

	if (err == 0)
		err = 6;  /* no DWARF information */
	return err;
}

static char *print_block(Dwarf_Block *block)
{
	int i;
	int len = block->length;
	char *result = g_malloc(len * 3 + 1);

	for (i = 0; i < len; i++) {
		snprintf(&result[i * 3], 4, "%02x ", block->data[i]);
	}

	return result;
}

static char *print_file_name(Dwarf_Die *cudie, int idx)
{
	Dwarf_Files *files;

	if (dwarf_getsrcfiles(cudie, &files, NULL) == 0)
		return g_strdup(dwarf_filesrc(files, idx, NULL, NULL));
	else
		return g_strdup_printf("Unknown file: %d\n", idx);
}

struct attr_arg {
	GtkTreeStore *store;
	Dwarf_Die cudie;
};

static int attr_callback(Dwarf_Attribute *attr, void *_arg)
{
	struct attr_arg *arg = _arg;
	GtkTreeStore *store = arg->store;
	GtkTreeIter iter;
	unsigned name = dwarf_whatattr(attr);
	unsigned form = dwarf_whatform(attr);
	unsigned long raw_value = 0;
	gpointer val_str = NULL;
	Dwarf_Block block;
	Dwarf_Word data;
	Dwarf_Addr addr;
	Dwarf_Off off;
	Dwarf_Die die;

	switch (form) {
	case DW_FORM_flag:
		raw_value = *attr->valp;
		val_str = g_strdup_printf("%s", raw_value ? "True" : "False");
		break;
	case DW_FORM_flag_present:
		raw_value = 1;
		val_str = g_strdup("True");
		break;
	case DW_FORM_string:
		val_str = g_strdup(attr->valp);
		break;
	case DW_FORM_strp:
	case DW_FORM_GNU_strp_alt:
		val_str = g_strdup(dwarf_formstring(attr));
		break;
	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_data4:
	case DW_FORM_data8:
	case DW_FORM_sdata:
	case DW_FORM_udata:
	case DW_FORM_sec_offset:
		dwarf_formudata(attr, &data);
		raw_value = data;
		if (name == DW_AT_decl_file || name == DW_AT_call_file)
			val_str = print_file_name(&arg->cudie, raw_value);
		else
			val_str = g_strdup_printf("%#x", raw_value);
		break;
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
	case DW_FORM_block:
	case DW_FORM_exprloc:
		dwarf_formblock(attr, &block);
		raw_value = block.length;
		val_str = print_block(&block);
		break;
	case DW_FORM_addr:
		dwarf_formaddr(attr, &addr);
		raw_value = addr;
		val_str = g_strdup_printf("%#x", raw_value);
		break;
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
		dwarf_formref(attr, &off);
		raw_value = off;
		val_str = g_strdup_printf("%#x", raw_value);
		break;
	case DW_FORM_ref_addr:
	case DW_FORM_ref_sig8:
	case DW_FORM_GNU_ref_alt:
		dwarf_formref_die(attr, &die);
		raw_value = dwarf_dieoffset(&die);
		val_str = g_strdup(dwarf_diename(&die));
		break;
	}

	gtk_tree_store_append(store, &iter, NULL);
	gtk_tree_store_set(store, &iter, 0, dwarview_attr_name(name),
			   1, dwarview_form_name(form),
			   2, raw_value, 3, val_str ?: "", -1);

	g_free(val_str);
	return DWARF_CB_OK;
}

static void on_row_activated(GtkTreeView *view, GtkTreePath *path,
			     GtkTreeViewColumn *col, gpointer data)
{
	GtkTreeView *attr_view = data;
	GtkTreeModel *main_model = gtk_tree_view_get_model(view);
	GtkTreeModel *attr_model = gtk_tree_view_get_model(attr_view);
	GtkTreeIter iter;
	GValue val = G_VALUE_INIT;
	Dwarf_Off off;
	Dwarf_Die die;
	struct attr_arg arg = {
		.store = GTK_TREE_STORE(attr_model),
	};

	gtk_tree_model_get_iter(main_model, &iter, path);
	gtk_tree_model_get_value(main_model, &iter, 0, &val);
	off = g_value_get_ulong(&val);
	g_value_unset(&val);

	if (dwarf_offdie(dwarf, off, &die) == NULL) {
		if ((int)off != -1)
			printf("bug?? %lx\n", off);
		return;
	}

	dwarf_diecu(&die, &arg.cudie, NULL, NULL);
	gtk_tree_store_clear(arg.store);
	dwarf_getattrs(&die, attr_callback, &arg, 0);
}

static gboolean on_button_press(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	GtkTreeView *view = GTK_TREE_VIEW(widget);
	GtkTreePath *path = NULL;
	bool expanded;

	/* double-click to toggle expand/collapse */
	if (event->button.type != GDK_2BUTTON_PRESS)
		return FALSE;

	gtk_tree_view_get_cursor(view, &path, NULL);

	expanded = gtk_tree_view_row_expanded(view, path);
	if (expanded)
		gtk_tree_view_collapse_row(view, path);
	else
		gtk_tree_view_expand_row(view, path, FALSE);

	gtk_tree_path_free(path);
	return TRUE;
}

static void add_gtk_callbacks(GtkBuilder *builder)
{
	gtk_builder_add_callback_symbol(builder, "on-row-activated",
					G_CALLBACK(on_row_activated));
	gtk_builder_add_callback_symbol(builder, "on-button-press",
					G_CALLBACK(on_button_press));
}

static const char *die_name(Dwarf_Die *die)
{
	return dwarf_hasattr(die, DW_AT_name) ? dwarf_diename(die) : "(no name)";
}

static void walk_die(Dwarf_Die *die, GtkTreeStore *store, GtkTreeIter *parent, int level)
{
	GtkTreeIter iter;
	Dwarf_Die next;

	gtk_tree_store_append(store, &iter, parent);
	gtk_tree_store_set(store, &iter, 0, dwarf_dieoffset(die),
			   1, dwarview_tag_name(dwarf_tag(die)),
			   2, die_name(die), -1);

	if (dwarf_haschildren(die)) {
		if (dwarf_child(die, &next)) {
			printf("buf?\n");
			return;
		}
		walk_die(&next, store, &iter, level+1);
	}

	if (level == 1)
		return;

	if (dwarf_siblingof(die, &next) != 0)
		return;

	walk_die(&next, store, parent, level);
}

static void add_contents(GtkBuilder *builder)
{
	Dwarf_Off off = 0;
	Dwarf_Off next;
	size_t sz;
	GtkTreeStore *main_store;
	GtkTreeStore *attr_store;

	main_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "main_store"));
	attr_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "attr_store"));

	while (dwarf_nextcu(dwarf, off, &next, &sz, NULL, NULL, NULL) == 0) {
		GtkTreeIter iter, func, vars, type, misc;
		Dwarf_Die die, child;

		if (dwarf_offdie(dwarf, off + sz, &die) == NULL) {
			printf("bug??\n");
			break;
		}

		gtk_tree_store_append(main_store, &iter, NULL);
		gtk_tree_store_set(main_store, &iter, 0, off + sz,
				   1, dwarview_tag_name(dwarf_tag(&die)),
				   2, dwarf_diename(&die), -1);

		gtk_tree_store_append(main_store, &func, &iter);
		gtk_tree_store_set(main_store, &func, 0, -1, 1, "meta", 2, "functions", -1);
		gtk_tree_store_append(main_store, &vars, &iter);
		gtk_tree_store_set(main_store, &vars, 0, -1, 1, "meta", 2, "variables", -1);
		gtk_tree_store_append(main_store, &type, &iter);
		gtk_tree_store_set(main_store, &type, 0, -1, 1, "meta", 2, "types", -1);
		gtk_tree_store_append(main_store, &misc, &iter);
		gtk_tree_store_set(main_store, &misc, 0, -1, 1, "meta", 2, "others", -1);

		if (dwarf_child(&die, &child) != 0)
			goto next;

		do {
			GtkTreeIter *parent;

			switch (dwarf_tag(&child)) {
			case DW_TAG_subprogram:
			case DW_TAG_inlined_subroutine:
			case DW_TAG_entry_point:
				parent = &func;
				break;
			case DW_TAG_base_type:
			case DW_TAG_array_type:
			case DW_TAG_class_type:
			case DW_TAG_enumeration_type:
			case DW_TAG_pointer_type:
			case DW_TAG_reference_type:
			case DW_TAG_string_type:
			case DW_TAG_structure_type:
			case DW_TAG_subroutine_type:
			case DW_TAG_union_type:
			case DW_TAG_set_type:
			case DW_TAG_subrange_type:
			case DW_TAG_const_type:
			case DW_TAG_file_type:
			case DW_TAG_packed_type:
			case DW_TAG_thrown_type:
			case DW_TAG_volatile_type:
			case DW_TAG_restrict_type:
			case DW_TAG_interface_type:
			case DW_TAG_unspecified_type:
			case DW_TAG_shared_type:
			case DW_TAG_ptr_to_member_type:
			case DW_TAG_rvalue_reference_type:
			case DW_TAG_typedef:
				parent = &type;
				break;
			case DW_TAG_variable:
			case DW_TAG_constant:
				parent = &vars;
				break;
			default:
				parent = &misc;
				break;
			}

			walk_die(&child, main_store, parent, 1);
		}
		while (dwarf_siblingof(&child, &child) == 0);

next:
		off = next;
	}
}

static void show_warning(GtkWidget *parent, const char *fmt, ...)
{
	GtkWidget *dialog;
	GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
	char *msg;
	va_list ap;

	va_start(ap, fmt);
	msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	dialog = gtk_message_dialog_new(GTK_WINDOW(parent), flags,
					GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					"%s", msg);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	g_free(msg);
}

int main(int argc, char *argv[])
{
	GtkBuilder *builder;
	GtkWidget  *window;

	gtk_init_check(&argc, &argv);

	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "dwarview.glade", NULL);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "root_window"));
	gtk_widget_show(window);

	add_gtk_callbacks(builder);
	gtk_builder_connect_signals(builder, NULL);

	if (argc > 1) {
		int err;

		err = open_dwarf_file(argv[1]);
		if (err != 0)
			show_warning(window, "Error: %s: %s\n",
				     argv[1], dwarf_errmsg(err));
		else
			add_contents(builder);
	}

	g_object_unref(builder);

	gtk_main();

	return 0;
}
