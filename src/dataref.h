/*
 * Copyright (C) 2013 Igalia S.L.
 *
 * Authors: Juan A. Suarez Romero <jasuarez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef _DATAREF_H_
#define _DATAREF_H_

#include <glib.h>

typedef struct _DataRef DataRef;

DataRef *dataref_new (gpointer data,
                      GDestroyNotify destroy_callback);

DataRef *dataref_ref (DataRef *dr);

void dataref_unref (DataRef *dr);

gpointer dataref_value (DataRef *dr);

#endif /* _DATAREF_H_*/
