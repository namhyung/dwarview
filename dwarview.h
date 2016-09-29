#ifndef DWARVIEW_H
#define DWARVIEW_H

#include <gelf.h>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>

#include <gtk/gtk.h>


#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(a[0]))

char *dwarview_tag_name(int tag);
char *dwarview_attr_name(unsigned int attr);
char *dwarview_form_name(unsigned int form);
char *dwarview_inline_name(unsigned int code);

#endif /* DWARVIEW_H */
