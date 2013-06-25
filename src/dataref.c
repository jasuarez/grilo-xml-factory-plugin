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

#include "dataref.h"

struct _DataRef {
  gint refcount;
  gpointer data;
  GDestroyNotify destroy;
};

DataRef *
dataref_new (gpointer data,
             GDestroyNotify destroy_callback)
{
  DataRef *dr;

  dr = g_slice_new (DataRef);
  dr->data = data;
  dr->refcount = 1;
  dr->destroy = destroy_callback;

  return dr;
}

DataRef *
dataref_ref (DataRef *dr)
{
  dr->refcount++;
  return dr;
}

void
dataref_unref (DataRef *dr)
{
  dr->refcount--;

  if (dr->refcount == 0) {
    if (dr->destroy && dr->data) {
      dr->destroy (dr->data);
    }

    g_slice_free (DataRef, dr);
  }
}

gpointer
dataref_value (DataRef *dr)
{
  return dr->data;
}
