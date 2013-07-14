/*
 * Copyright (C) 2013 Igalia S.L.
 *
 * Contact: Iago Toral Quiroga <itoral@igalia.com>
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

#ifndef _JSON_GHASHTABLE_H_
#define _JSON_GHASHTABLE_H_

#include <glib.h>
#include <json-glib/json-glib.h>

JsonNode *json_ghashtable_serialize (GHashTable *table);

gchar *json_ghashtable_serialize_data (GHashTable *table,
                                       gsize *length);

GHashTable *json_ghashtable_deserialize (JsonNode *node,
                                         GError **error);

GHashTable *json_ghashtable_deserialize_data (const gchar *data,
                                              gsize length,
                                              GError **error);

#endif /* _JSON_GHASHTABLE_H_ */
