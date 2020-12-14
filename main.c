/*
 * dwarview - DWARF debug info viewer
 *
 * Copyright (C) 2016  Namhyung Kim <namhyung@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 or later of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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

static GtkBuilder *builder;

struct content_arg {
	char *filename;
	GtkBuilder *builder;
	GtkTreeStore *main_store;
	GtkTreeStore *attr_store;
	GtkStatusbar *status;
	guint status_ctx;
	Dwarf_Off off;
	size_t total_size;
	char msgbuf[4096];
};

static struct content_arg *arg;

struct search_status {
	bool on_going;
	bool try_var;
	bool with_decl;
	gint found;
	guint ctx_id;
	GList *curr;
	gchar *text;
	GPatternSpec *patt;

	GtkSearchEntry *entry;
	GtkButton *button;
	GtkToggleButton *func;
	GtkToggleButton *var;
	GtkToggleButton *decl;
	GtkTreeView *result;
	GtkTreeView *main_view;
	GtkStatusbar *status;

	char msgbuf[4096];
};

static struct search_status *search;

static GList *func_list;
static GList *func_first;
static GList *var_list;
static GList *var_first;

struct search_item {
	char *name;
	GtkTreePath *path;
};

static GHashTable *die_map;

static void add_contents(GtkBuilder *builder, char *filename);
static void destroy_item(gpointer data);

extern void setup_demangler(void);
extern void finish_demangler(void);
extern bool demangler_enabled(void);
extern int demangle(const char *input, char *output, int outlen);

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
		return errno;

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

static void close_dwarf_file(void)
{
	dwarf_end(dwarf);

	gtk_tree_store_clear(arg->main_store);
	gtk_tree_store_clear(arg->attr_store);

	gtk_tree_store_clear(GTK_TREE_STORE(gtk_tree_view_get_model(search->result)));

	g_list_free_full(func_list, destroy_item);
	g_list_free_full(var_list, destroy_item);
	func_list = NULL;
	var_list = NULL;

	g_hash_table_destroy(die_map);
	die_map = NULL;

	/* stop and re-enable search */
	search->on_going = FALSE;
	g_object_set(search->entry, "editable", TRUE, NULL);
	gtk_button_set_label(search->button, "Search");

	gtk_statusbar_pop(arg->status, arg->status_ctx);
	gtk_statusbar_pop(search->status, search->ctx_id);

	g_free(search->text);
	search->text = NULL;
	if (search->patt) {
		g_pattern_spec_free(search->patt);
		search->patt = NULL;
	}

	g_free(arg->filename);

	g_free(arg);
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

static Elf_Data *get_elf_secdata(Elf *elf, char *sec_name)
{
	size_t i, num_sec, strndx;
	Elf_Scn *sec;

	elf_getshdrnum(elf, &num_sec);
	elf_getshdrstrndx(elf, &strndx);

	for (i = 0; i < num_sec; i++) {
		char *name;
		GElf_Shdr shdr;

		sec = elf_getscn(elf, i);
		gelf_getshdr(sec, &shdr);
		name = elf_strptr(elf, strndx, shdr.sh_name);

		if (!g_strcmp0(name, sec_name)) {
			return elf_getdata(sec, NULL);
		}
	}
	return NULL;
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

long read_sleb128(unsigned char *data, int *nbytes)
{
	int n = 1;
	long val = *data & 0x7f;

	while (*data & 0x80) {
		data++;

		val |= ((unsigned long)(*data & 0x7f) << (7 * n));
		n++;
	}

	if (val & (1UL << (7 * n - 1)))
		val |= (-1UL << (7 * n));  /* sign extension */

	*nbytes = n;
	return val;
}

static const char *get_regname(int regno)
{
	static char buf[32];
	static const char *gp_regs[] = {
		"rax", "rdx", "rcx", "rbx", "rsi", "rdi", "rbp", "rsp",
	};
	const char *reg = buf;

	switch (regno) {
	case 0 ... 7:
		reg = gp_regs[regno];
		break;
	case 8 ... 15:
		snprintf(buf, sizeof(buf), "r%d", regno);
		break;
	case 16:
		reg = "RA";  /* return address ??? */
		break;
	case 17 ... 32:
		snprintf(buf, sizeof(buf), "xmm%d", regno - 17);
		break;
	case 33 ... 40:
		snprintf(buf, sizeof(buf), "st%d", regno - 33);
		break;
	case 41 ... 48:
		snprintf(buf, sizeof(buf), "mm%d", regno - 41);
		break;
	default:
		reg = "unknown";
		break;
	}

	return reg;
}

static char *print_exprloc(Dwarf_Block *block)
{
	int i, n, k;
	int len = block->length;
	int pos = 0;
	int size = 4096;
	char *result = g_malloc(size);
	long sarg;
	unsigned long uarg;

	for (i = 0; i < len; i++) {
		switch (block->data[i]) {
		case 0x03:
			memcpy(&uarg, block->data + i + 1, sizeof(uarg));
			pos += snprintf(result + pos, size - pos, "03 ");
			for (k = 0; k < sizeof(uarg); k++)
				pos += snprintf(result + pos, size - pos,
						"%02x ", block->data[i + k + 1]);
			pos += snprintf(result + pos, size - pos,
					"(addr %#lx) ", uarg);
			i += sizeof(uarg);
			break;
		case 0x06:
			pos += snprintf(result + pos, size - pos,
					"06 (deref) ");
			break;
		case 0x30 ... 0x4f:
			pos += snprintf(result + pos, size - pos,
					"%02x (literal %d) ", block->data[i],
					block->data[i] - 0x30);
			break;
		case 0x50 ... 0x6f:
			pos += snprintf(result + pos, size - pos,
					"%02x (reg%d: %s) ", block->data[i],
					block->data[i] - 0x50,
					get_regname(block->data[i] - 0x50));
			break;
		case 0x70 ... 0x8f:
			sarg = read_sleb128(block->data + i + 1, &n);
			pos += snprintf(result + pos, size - pos,
					"%02x ", block->data[i]);
			for (k = 0; k < n; k++)
				pos += snprintf(result + pos, size - pos,
						"%02x ", block->data[i + k + 1]);
			pos += snprintf(result + pos, size - pos,
					"(%s%+ld) ", get_regname(block->data[i] - 0x70), sarg);
			i += n;
			break;
		case 0x91:
			sarg = read_sleb128(block->data + i + 1, &n);
			pos += snprintf(result + pos, size - pos, "91 ");
			for (k = 0; k < n; k++)
				pos += snprintf(result + pos, size - pos,
						"%02x ", block->data[i + k + 1]);
			pos += snprintf(result + pos, size - pos,
					"(fbreg%+ld) ", sarg);
			i += n;
			break;
		case 0x96:
			pos += snprintf(result + pos, size - pos,
					"96 (nop) ");
			break;
		case 0x9c:
			pos += snprintf(result + pos, size - pos,
					"9c (cfa) ");
			break;
		default:
			pos += snprintf(result + pos, size - pos,
					"%02x ", block->data[i]);
			break;
		}
	}

	return result;
}

static char *print_file_name(Dwarf_Die *die, int idx)
{
	Dwarf_Files *files;
	const gchar *dir_prefix = NULL;
	Dwarf_Word file_idx;
	Dwarf_Attribute attr;
	Dwarf_Die cudie;

	dwarf_diecu(die, &cudie, NULL, NULL);

	if (dwarf_attr(&cudie, DW_AT_comp_dir, &attr))
		dir_prefix = dwarf_formstring(&attr);

	if (dwarf_getsrcfiles(&cudie, &files, NULL) == 0) {
		const gchar *short_name = NULL;
		const gchar *full_path = dwarf_filesrc(files, idx, NULL, NULL);

		if (g_str_has_prefix(full_path, dir_prefix)) {
			short_name = full_path + strlen(dir_prefix);
			while (*short_name == '/')
				short_name++;
		}

		return g_strdup(short_name ?: full_path);
	}
	else
		return g_strdup_printf("Unknown file: %d\n", idx);
}

static char *print_addr_ranges(Dwarf_Die *die)
{
	ptrdiff_t offset = 0;
	Dwarf_Addr base, start, end;
	char *result = NULL;

	while ((offset = dwarf_ranges(die, offset, &base, &start, &end)) > 0) {
		result = g_strdup_printf("%s%s[%lx,%lx)", result ?: "",
					 result ? ", " : "", start, end);
	}

	return result;
}

static char *die_location(Dwarf_Die *die)
{
	gchar *file = NULL;
	gint line = 0;

	if (dwarf_hasattr(die, DW_AT_decl_file) || dwarf_hasattr(die, DW_AT_call_file)) {
		Dwarf_Die cudie;
		Dwarf_Word file_idx;
		Dwarf_Attribute attr;

		dwarf_diecu(die, &cudie, NULL, NULL);

		if (dwarf_attr(die, DW_AT_decl_file, &attr) == NULL)
			dwarf_attr(die, DW_AT_call_file, &attr);

		dwarf_formudata(&attr, &file_idx);
		file = print_file_name(&cudie, file_idx);
	}
	if (dwarf_hasattr(die, DW_AT_decl_line) || dwarf_hasattr(die, DW_AT_call_line)) {
		Dwarf_Word lineno;
		Dwarf_Attribute attr;

		if (dwarf_attr(die, DW_AT_decl_line, &attr) == NULL)
			dwarf_attr(die, DW_AT_call_line, &attr);

		dwarf_formudata(&attr, &lineno);
		line = lineno;
	}

	return g_strdup_printf(" in %s:%d", file ?: "(unknown)", line);
}

static char *type_name(Dwarf_Die *die)
{
	char *type = NULL;
	char *name = NULL;
	int tag = dwarf_tag(die);
	Dwarf_Off off;
	Dwarf_Die ref;
	Dwarf_Attribute attr;

	if (dwarf_hasattr(die, DW_AT_name))
		name = (char *)dwarf_diename(die);

	switch (tag) {
	case DW_TAG_structure_type:
		type = "struct";
		break;
	case DW_TAG_union_type:
		type = "union";
		break;
	case DW_TAG_enumeration_type:
		type = "enum";
		break;
	case DW_TAG_class_type:
		type = "class";
		break;
	case DW_TAG_interface_type:
		type = "interface";
		break;
	case DW_TAG_subroutine_type:
		type = "function";
		break;
	default:
		break;
	}

	if (type || name)
		return g_strdup_printf("%s%s%s", type ?: "",
				       (type && name) ? " " : "", name ?: "");

	if (!dwarf_hasattr(die, DW_AT_type))
		return g_strdup_printf("no type");

	dwarf_attr(die, DW_AT_type, &attr);

	switch (dwarf_whatform(&attr)) {
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
	case DW_FORM_ref_addr:
	case DW_FORM_ref_sig8:
	case DW_FORM_GNU_ref_alt:
		dwarf_formref_die(&attr, &ref);
		name = type_name(&ref);
		break;
	default:
		name = strdup("");
		break;
	}

	switch (tag) {
	case DW_TAG_const_type:
		type = g_strdup_printf("const %s", name);
		break;
	case DW_TAG_volatile_type:
		type = g_strdup_printf("volatile %s", name);
		break;
	case DW_TAG_restrict_type:
		type = g_strdup_printf("restrict %s", name);
		break;
	case DW_TAG_pointer_type:
	case DW_TAG_ptr_to_member_type:
		type = g_strdup_printf("pointer to %s", name);
		break;
	case DW_TAG_reference_type:
	case DW_TAG_rvalue_reference_type:
		type = g_strdup_printf("reference to %s", name);
		break;
	case DW_TAG_array_type:
		type = g_strdup_printf("array of %s", name);
		break;
	default:
		type = g_strdup_printf("unknown type (%d)", tag);
		break;
	}

	free(name);
	return type;
}

struct attr_arg {
	GtkTreeStore *store;
	Dwarf_Die *diep;
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
			val_str = print_file_name(arg->diep, raw_value);
		else if (name == DW_AT_decl_line || name == DW_AT_call_line)
			val_str = g_strdup_printf("Line %lu", raw_value);
		else if (name == DW_AT_inline)
			val_str = g_strdup(dwarview_inline_name(raw_value));
		else if (name == DW_AT_ranges)
			val_str = print_addr_ranges(arg->diep);
		else if (name == DW_AT_language)
			val_str = g_strdup(dwarview_language_name(raw_value));
		else
			val_str = g_strdup_printf("%#lx", raw_value);
		break;
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
	case DW_FORM_block:
	case DW_FORM_exprloc:
		dwarf_formblock(attr, &block);
		raw_value = block.length;
		if (form == DW_FORM_exprloc)
			val_str = print_exprloc(&block);
		else
			val_str = print_block(&block);
		break;
	case DW_FORM_addr:
		dwarf_formaddr(attr, &addr);
		raw_value = addr;
		val_str = g_strdup_printf("%#lx", raw_value);
		break;
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
	case DW_FORM_ref_addr:
	case DW_FORM_ref_sig8:
	case DW_FORM_GNU_ref_alt:
		dwarf_formref(attr, &off);
		raw_value = off;

		dwarf_formref_die(attr, &die);

		if (name == DW_AT_type) {
			char *type = type_name(&die);

			val_str = g_strdup_printf("%#lx (%s)", raw_value, type);
			free(type);
		}
		else if (dwarf_diename(&die)) {
			val_str = g_strdup_printf("%#lx (%s)", raw_value,
						  dwarf_diename(&die));
		}
		else
			val_str = g_strdup_printf("%#lx", raw_value);
		break;
	}

	gtk_tree_store_append(store, &iter, NULL);
	gtk_tree_store_set(store, &iter, 0, dwarview_attr_name(name),
			   1, dwarview_form_name(form),
			   2, raw_value, 3, val_str ?: "", -1);

	g_free(val_str);
	return DWARF_CB_OK;
}

static void on_cursor_changed(GtkTreeView *view, gpointer data)
{
	GtkTreeView *attr_view = data;
	GtkTreeModel *main_model = gtk_tree_view_get_model(view);
	GtkTreeModel *attr_model = gtk_tree_view_get_model(attr_view);
	GtkTreeIter iter;
	GValue val = G_VALUE_INIT;
	Dwarf_Off off;
	Dwarf_Die die;
	struct attr_arg arg = {
		.diep = &die,
		.store = GTK_TREE_STORE(attr_model),
	};
	char buf[32];

	GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
	gtk_tree_selection_get_selected(selection, NULL, &iter);
	gtk_tree_model_get_value(main_model, &iter, 0, &val);
	off = strtoul(g_value_get_string(&val), NULL, 0);
	g_value_unset(&val);

	if (dwarf_offdie(dwarf, off, &die) == NULL) {
		if ((int)off != -1)
			printf("bug?? %lx\n", off);
		return;
	}

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

#define MAX_SEARCH_COUNT  1000


static void stop_search(struct search_status *search, const gchar *msg);

static int do_search(struct search_status *search, struct search_item *item)
{
	GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(search->result));
	GtkTreeModel *main_model = gtk_tree_view_get_model(search->main_view);
	GtkTreeIter iter, main_iter;
	GValue val = G_VALUE_INIT;
	Dwarf_Off off;
	Dwarf_Die die;
	gchar *location;

	if (!g_pattern_match_string(search->patt, item->name))
		return 0;

	gtk_tree_model_get_iter(main_model, &main_iter, item->path);
	gtk_tree_model_get_value(main_model, &main_iter, 0, &val);
	off = strtoul(g_value_get_string(&val), NULL, 0);
	g_value_unset(&val);

	if (dwarf_offdie(dwarf, off, &die) == NULL)
		return -1;

	if (dwarf_hasattr(&die, DW_AT_declaration) && !search->with_decl)
		return 0;

	location = die_location(&die);

	gtk_tree_store_append(store, &iter, NULL);
	gtk_tree_store_set(store, &iter, 0, item->name, 1, location, 2, item->path, -1);
	g_free(location);

	search->found++;
	g_snprintf(search->msgbuf, sizeof(search->msgbuf), "Searching '%s' ... (found %d)",
		   search->text, search->found);

	gtk_statusbar_pop(search->status, search->ctx_id);
	gtk_statusbar_push(search->status, search->ctx_id, search->msgbuf);

	return 0;
}

static guint search_handler(void *arg)
{
	struct search_status *search = arg;
	int count = 0;
	GList *curr = search->curr;

	if (!search->on_going)
		return FALSE;

	while (curr) {
		struct search_item *item = curr->data;

		if (do_search(search, item) < 0) {
			char tmp[1024];

err:
			g_snprintf(tmp, sizeof(tmp), "Failed (at %s).", item->name);
			stop_search(search, tmp);
			return FALSE;
		}

		curr = g_list_previous(curr);

		if (++count == MAX_SEARCH_COUNT)
			goto out;
	}

	if (search->try_var) {
		curr = var_first;
		search->try_var = FALSE;
	}

	while (curr) {
		struct search_item *item = curr->data;

		if (do_search(search, item) < 0)
			goto err;

		curr = g_list_previous(curr);

		if (++count == MAX_SEARCH_COUNT)
			break;
	}

out:
	search->curr = curr;

	if (curr == NULL) {
		char tmp[1024];

		g_snprintf(tmp, sizeof(tmp), "Done (%d found).", search->found);
		stop_search(search, tmp);
	}

	return curr != NULL;
}

static void start_search(struct search_status *search, const gchar *text)
{
	/* delete previous result */
	gtk_tree_store_clear(GTK_TREE_STORE(gtk_tree_view_get_model(search->result)));
	search->found = 0;

	g_free(search->text);
	if (search->patt)
		g_pattern_spec_free(search->patt);

	search->text = g_strdup(text);
	search->patt = g_pattern_spec_new(text);

	search->try_var = FALSE;
	if (gtk_toggle_button_get_active(search->func)) {
		search->curr = func_first;
		if (gtk_toggle_button_get_active(search->var))
			search->try_var = TRUE;
	}
	else
		search->curr = var_first;

	search->with_decl = gtk_toggle_button_get_active(search->decl);

	search->on_going = TRUE;
	g_idle_add((GSourceFunc)search_handler, search);
}

static void stop_search(struct search_status *search, const gchar *msg)
{
	search->on_going = FALSE;
	g_object_set(search->entry, "editable", TRUE, NULL);
	gtk_button_set_label(search->button, "Search");

	g_snprintf(search->msgbuf, sizeof(search->msgbuf), "Searching '%s' ... %s",
		   search->text, msg);

	gtk_statusbar_pop(search->status, search->ctx_id);
	gtk_statusbar_push(search->status, search->ctx_id, search->msgbuf);
}

static void on_search_activated(GtkEntry *entry, gpointer data)
{
	gtk_button_clicked(GTK_BUTTON(data));
}

static void on_search_button(GtkButton *button, gpointer data)
{
	struct search_status *search = data;
	GtkEntry *entry = GTK_ENTRY(search->entry);
	const gchar *text;

	if (!search->on_going) {
		/* at least one of the check boxes should be set */
		if (!gtk_toggle_button_get_active(search->func) &&
		    !gtk_toggle_button_get_active(search->var))
			return;

		text = gtk_entry_get_text(entry);
		if (*text && g_strcmp0(text, search->text)) {
			start_search(search, text);
			g_object_set(entry, "editable", FALSE, NULL);
			gtk_button_set_label(button, "Stop");
		}
	}
	else {
		stop_search(search, "Canceled");
	}
}

static void on_search_result(GtkTreeView *view, GtkTreePath *path,
			     GtkTreeViewColumn *col, gpointer data)
{
	GtkTreeView *main_view = data;
	GtkTreeModel *model = gtk_tree_view_get_model(view);
	GtkTreeIter iter;
	GtkTreePath *main_path;
	GValue val = G_VALUE_INIT;
	gboolean expanded;

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get_value(model, &iter, 2, &val);
	main_path = g_value_get_pointer(&val);
	g_value_unset(&val);

	expanded = gtk_tree_view_row_expanded(main_view, main_path);

	gtk_tree_view_expand_to_path(main_view, main_path);
	gtk_tree_view_scroll_to_cell(main_view, main_path, NULL, TRUE, 0.5, 0);  /* center align */
	gtk_tree_view_set_cursor(main_view, main_path, NULL, FALSE);
	gtk_tree_view_row_activated(main_view, main_path, NULL);

	/* do not change 'expanded' status */
	if (!expanded)
		gtk_tree_view_collapse_row(main_view, main_path);
}

static void setup_search_status(GtkBuilder *builder)
{
	search = g_malloc(sizeof(*search));

	search->on_going = FALSE;
	search->text = NULL;
	search->patt = NULL;
	search->entry = GTK_SEARCH_ENTRY(gtk_builder_get_object(builder, "search_entry"));
	search->button = GTK_BUTTON(gtk_builder_get_object(builder, "search_btn"));
	search->func = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "search_func"));
	search->var = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "search_var"));
	search->decl = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "search_decl"));
	search->result = GTK_TREE_VIEW(gtk_builder_get_object(builder, "search_view"));
	search->main_view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "main_view"));

	search->status = GTK_STATUSBAR(gtk_builder_get_object(builder, "status"));
	search->ctx_id = gtk_statusbar_get_context_id(search->status, "search context");

	g_snprintf(search->msgbuf, sizeof(search->msgbuf), "...");
	gtk_statusbar_push(search->status, search->ctx_id, search->msgbuf);

	g_signal_connect(G_OBJECT(search->button), "clicked", (GCallback)on_search_button, search);
}

static void on_file_open(GtkMenuItem *menu, gpointer *window)
{
	int res;
	gchar *filename;
	GtkWidget *dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	GtkFileChooser *chooser;

	if (dwarf != NULL)
		return;

	dialog = gtk_file_chooser_dialog_new("Open File", GTK_WINDOW(window), action,
					      "_Cancel", GTK_RESPONSE_CANCEL,
					      "_Open", GTK_RESPONSE_ACCEPT,
					      NULL);

	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (dialog);
		return;
	}

	chooser = GTK_FILE_CHOOSER(dialog);
	filename = gtk_file_chooser_get_filename(chooser);
	gtk_widget_destroy (dialog);

	res = open_dwarf_file(filename);
	if (res != 0)
		show_warning(GTK_WIDGET(window), "Error: %s: %s\n",
			     filename, dwarf_errmsg(res));
	else
		add_contents(builder, filename);
}

static void on_file_close(GtkMenuItem *menu, gpointer *unused)
{
	if (dwarf == NULL)
		return;

	close_dwarf_file();
	dwarf = NULL;
}

static gboolean on_attr_press(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	GtkTreeView *view = GTK_TREE_VIEW(widget);
	GtkTreePath *path = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model(view);
	GtkTreeView *main_view = data;
	GtkTreeIter iter;
	GValue val = G_VALUE_INIT;
	const char *type;
	unsigned long off;
	bool ref = FALSE;
	bool expanded;

	/* double-click to follow reference (jump to offset) */
	if (event->button.type != GDK_2BUTTON_PRESS)
		return FALSE;

	gtk_tree_view_get_cursor(view, &path, NULL);
	if (path == NULL)
		return FALSE;

	gtk_tree_model_get_iter(model, &iter, path);

	gtk_tree_model_get_value(model, &iter, 1, &val);
	type = g_value_get_string(&val);
	if (!strncmp(type, "ref", 3))
		ref = TRUE;
	g_value_unset(&val);

	if (!ref)
		return FALSE;

	gtk_tree_model_get_value(model, &iter, 2, &val);
	off = g_value_get_ulong(&val);
	g_value_unset(&val);

	gtk_tree_path_free(path);

	path = g_hash_table_lookup(die_map, (void *)off);
	if (path == NULL)
		return FALSE;

	expanded = gtk_tree_view_row_expanded(main_view, path);

	gtk_tree_view_expand_to_path(main_view, path);
	gtk_tree_view_scroll_to_cell(main_view, path, NULL, TRUE, 0.5, 0);  /* center align */
	gtk_tree_view_set_cursor(main_view, path, NULL, FALSE);
	gtk_tree_view_row_activated(main_view, path, NULL);

	/* do not change 'expanded' status */
	if (!expanded)
		gtk_tree_view_collapse_row(main_view, path);

	return TRUE;
}

static void add_gtk_callbacks(GtkBuilder *builder)
{
	gtk_builder_add_callback_symbol(builder, "on-file-open",
					G_CALLBACK(on_file_open));
	gtk_builder_add_callback_symbol(builder, "on-file-close",
					G_CALLBACK(on_file_close));
	gtk_builder_add_callback_symbol(builder, "on-cursor-changed",
					G_CALLBACK(on_cursor_changed));
	gtk_builder_add_callback_symbol(builder, "on-button-press",
					G_CALLBACK(on_button_press));
	gtk_builder_add_callback_symbol(builder, "on-search-activated",
					G_CALLBACK(on_search_activated));
	gtk_builder_add_callback_symbol(builder, "on-search-result",
					G_CALLBACK(on_search_result));
	gtk_builder_add_callback_symbol(builder, "on-attr-press",
					G_CALLBACK(on_attr_press));

	setup_search_status(builder);
}

static const char *die_name(Dwarf_Die *die)
{
	Dwarf_Off off;
	Dwarf_Die pos = *die;
	Dwarf_Die origin;
	Dwarf_Attribute attr;
	const char *name;
	static char buf[4096];

	switch (dwarf_tag(die)) {
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
	case DW_TAG_enumeration_type:
	case DW_TAG_class_type:
	case DW_TAG_interface_type:
	case DW_TAG_subroutine_type:
	case DW_TAG_const_type:
	case DW_TAG_volatile_type:
	case DW_TAG_restrict_type:
	case DW_TAG_pointer_type:
	case DW_TAG_ptr_to_member_type:
	case DW_TAG_reference_type:
	case DW_TAG_rvalue_reference_type:
	case DW_TAG_array_type:
		return type_name(die);
	default:
		break;
	}

	while (true) {
		if (dwarf_attr(&pos, DW_AT_name, &attr))
			return dwarf_formstring(&attr);
		/* use linkage name only if it can demangle the name */
		if (dwarf_attr(&pos, DW_AT_linkage_name, &attr) && demangler_enabled()) {
			demangle(dwarf_formstring(&attr), buf, sizeof(buf));
			return buf;
		}

		if (dwarf_attr(&pos, DW_AT_abstract_origin, &attr) == NULL &&
		    dwarf_attr(&pos, DW_AT_specification, &attr) == NULL &&
		    dwarf_attr(&pos, DW_AT_import, &attr) == NULL)
			goto out;

		if (dwarf_formref_die(&attr, &origin) == NULL)
			goto out;

		pos = origin;
	}
out:
	return "(no name)";
}

static void walk_die(Dwarf_Die *die, GtkTreeStore *store, GtkTreeIter *parent, int level)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	Dwarf_Die next;
	int tag = dwarf_tag(die);
	const gchar *name = die_name(die);
	gchar *markup = NULL;
	char buf[32];

	if (dwarf_hasattr(die, DW_AT_declaration) || tag == DW_TAG_imported_declaration) {
		const char *decl = "(decl)";

		if (tag == DW_TAG_imported_declaration)
			decl = "";

		markup = g_strdup_printf("<span foreground=\"grey\">%s %s</span>",
					 dwarview_tag_name(tag), decl, -1);
	}

	snprintf(buf, sizeof(buf), "%#lx", dwarf_dieoffset(die));
	gtk_tree_store_append(store, &iter, parent);
	gtk_tree_store_set(store, &iter, 0, buf,
			   1, markup ?: dwarview_tag_name(tag),
			   2, name, -1);
	g_free(markup);

	path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
	g_hash_table_insert(die_map, (void *)dwarf_dieoffset(die), path);

	/* currently function and variable type can be searched */
	switch (tag) {
	case DW_TAG_subprogram:
	case DW_TAG_inlined_subroutine:
	case DW_TAG_entry_point:
		if (g_strcmp0(name, "(no name)")) {
			struct search_item *item = g_malloc(sizeof(*item));
			bool is_first = (func_list == NULL);

			item->name = g_strdup(name);
			item->path = path;
			func_list = g_list_prepend(func_list, item);

			if (is_first)
				func_first = func_list;
		}
		break;
	case DW_TAG_variable:
	case DW_TAG_constant:
		if (g_strcmp0(name, "(no name)")) {
			struct search_item *item = g_malloc(sizeof(*item));
			bool is_first = (var_list == NULL);

			item->name = g_strdup(name);
			item->path = path;
			var_list = g_list_prepend(var_list, item);

			if (is_first)
				var_first = var_list;
		}
		break;
	default:
		break;
	}

	if (dwarf_haschildren(die)) {
		if (dwarf_child(die, &next)) {
			printf("bug?\n");
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

static void destroy_item(gpointer data)
{
	struct search_item *item = data;

	gtk_tree_path_free(item->path);
	g_free(item->name);
	g_free(item);
}

static guint add_die_content(void *_arg)
{
	struct content_arg *arg = _arg;
	GtkTreeStore *main_store = arg->main_store;
	GtkTreeIter iter, func, vars, type, misc;
	Dwarf_Off off = arg->off;
	Dwarf_Off next;
	Dwarf_Die die, child;
	size_t sz;
	char buf[32];

	if (dwarf_nextcu(dwarf, off, &next, &sz, NULL, NULL, NULL)) {
		gtk_statusbar_pop(arg->status, arg->status_ctx);
		g_snprintf(arg->msgbuf, sizeof(arg->msgbuf), "Opening %s ... Done!", arg->filename);
		gtk_statusbar_push(arg->status, arg->status_ctx, arg->msgbuf);
		return FALSE;
	}

	if (dwarf_offdie(dwarf, off + sz, &die) == NULL) {
		g_snprintf(arg->msgbuf, sizeof(arg->msgbuf), "Error: cannot find offset %lx for %s",
			   off + sz, arg->filename);
		gtk_statusbar_push(arg->status, arg->status_ctx, arg->msgbuf);
		return FALSE;
	}

	snprintf(buf, sizeof(buf), "%#lx", off + sz);
	gtk_tree_store_append(main_store, &iter, NULL);
	gtk_tree_store_set(main_store, &iter, 0, buf,
			   1, dwarview_tag_name(dwarf_tag(&die)),
			   2, dwarf_diename(&die), -1);

	g_hash_table_insert(die_map, (void *)(off + sz),
			    gtk_tree_model_get_path(GTK_TREE_MODEL(main_store), &iter));

	gtk_tree_store_append(main_store, &func, &iter);
	gtk_tree_store_set(main_store, &func, 0, "", 1, "meta", 2, "functions", -1);
	gtk_tree_store_append(main_store, &vars, &iter);
	gtk_tree_store_set(main_store, &vars, 0, "", 1, "meta", 2, "variables", -1);
	gtk_tree_store_append(main_store, &type, &iter);
	gtk_tree_store_set(main_store, &type, 0, "", 1, "meta", 2, "types", -1);
	gtk_tree_store_append(main_store, &misc, &iter);
	gtk_tree_store_set(main_store, &misc, 0, "", 1, "meta", 2, "others", -1);

	arg->off = next;
	if (dwarf_child(&die, &child) != 0)
		return TRUE;

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
			parent = &vars;
			break;
		default:
			parent = &misc;
			break;
		}

		gtk_statusbar_pop(arg->status, arg->status_ctx);
		g_snprintf(arg->msgbuf, sizeof(arg->msgbuf), "Opening %s ... (%lu/%lu)",
			   arg->filename, off, arg->total_size);
		gtk_statusbar_push(arg->status, arg->status_ctx, arg->msgbuf);
		walk_die(&child, main_store, parent, 1);
	}
	while (dwarf_siblingof(&child, &child) == 0);

	return TRUE;
}

static void add_contents(GtkBuilder *builder, char *filename)
{
	Elf_Data *data;

	arg = g_malloc(sizeof(*arg));
	arg->builder = builder;
	arg->filename = filename;
	arg->off = 0;

	arg->main_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "main_store"));
	arg->attr_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "attr_store"));
	arg->status = GTK_STATUSBAR(gtk_builder_get_object(builder, "status"));

	arg->status_ctx = gtk_statusbar_get_context_id(arg->status, "default context");

	g_snprintf(arg->msgbuf, sizeof(arg->msgbuf), "Opening %s ...", filename);
	gtk_statusbar_push(arg->status, arg->status_ctx, arg->msgbuf);

	die_map = g_hash_table_new(g_direct_hash, g_direct_equal);

	data = get_elf_secdata(dwarf_getelf(dwarf), ".debug_info");
	if (data)
		arg->total_size = data->d_size;
	else
		arg->total_size = -1;  /* XXX */

	g_idle_add((GSourceFunc)add_die_content, arg);
}

static int try_add_builder(GtkBuilder *builder)
{
	char buf[4096];
	const char * sysdir_list[] = {
		"/usr/local/share",
		"/usr/share",
	};
	const char filename[] = "dwarview.glade";
	size_t sz = sizeof(buf);
	unsigned i;

	/* check current directory for local development */
	if (gtk_builder_add_from_file(builder, filename, NULL) > 0)
		return 0;

	if (getenv("XDG_DATA_HOME")) {
		snprintf(buf, sz, "%s/dwarview/%s", getenv("XDG_DATA_HOME"), filename);

		if (gtk_builder_add_from_file(builder, buf, NULL) > 0)
			return 0;
	}

	if (getenv("HOME")) {
		snprintf(buf, sz, "%s/.local/share/dwarview/%s", getenv("HOME"), filename);

		if (gtk_builder_add_from_file(builder, buf, NULL) > 0)
			return 0;
	}

	for (i = 0; i < ARRAY_SIZE(sysdir_list); i++) {
		snprintf(buf, sz, "%s/dwarview/%s", sysdir_list[i], filename);

		if (gtk_builder_add_from_file(builder, buf, NULL) > 0)
			return 0;
	}

	return -1;
}

int main(int argc, char *argv[])
{
	GtkWidget  *window;

	gtk_init_check(&argc, &argv);

	builder = gtk_builder_new();
	if (try_add_builder(builder) < 0) {
		printf("failed to find UI description\n");
		return 1;
	}

	setup_demangler();

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
			add_contents(builder, g_strdup(argv[1]));
	}

	gtk_main();

	finish_demangler();

	return 0;
}
