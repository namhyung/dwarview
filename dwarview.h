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
char *dwarview_language_name(unsigned int code);

#endif /* DWARVIEW_H */
