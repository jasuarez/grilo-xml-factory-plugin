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

#include "json-ghashtable.h"

static void
_serialize_entry (gchar *key,
                  gchar *value,
                  JsonBuilder *builder)
{
  json_builder_set_member_name (builder, key);
  json_builder_add_string_value (builder, value);
}

JsonNode *
json_ghashtable_serialize (GHashTable *table)
{
  JsonBuilder *builder;
  JsonNode *root;

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  if (table) {
    g_hash_table_foreach (table, (GHFunc) _serialize_entry, builder);
  }

  json_builder_end_object (builder);

  root = json_builder_get_root (builder);

  g_object_unref (builder);

  return root;
}

gchar *
json_ghashtable_serialize_data (GHashTable *table,
                                gsize *length)
{
  JsonGenerator *generator;
  JsonNode *root;
  gchar *serial;

  generator = json_generator_new ();
  root = json_ghashtable_serialize (table);
  json_generator_set_root (generator, root);
  serial = json_generator_to_data (generator, length);
  json_node_free (root);
  g_object_unref (generator);

  return serial;
}

GHashTable *
json_ghashtable_deserialize (JsonNode *node,
                             GError **error)
{
  GHashTable *table = NULL;
  GList *k;
  GList *keys;
  JsonNode *node_value;
  JsonObject *object;
  gchar *value;

  if (!node || !JSON_NODE_HOLDS_OBJECT (node)) {
    g_set_error_literal (error,
                         JSON_READER_ERROR,
                         JSON_READER_ERROR_NO_OBJECT,
                         "Expected JSON object");
    return table;
  }

  object = json_node_get_object (node);
  keys = json_object_get_members (object);
  table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  for (k = keys; k ; k = g_list_next (k)) {
    node_value = json_object_get_member (object, k->data);
    if (!JSON_NODE_HOLDS_VALUE (node_value) ||
        (value = json_node_dup_string (node_value)) == NULL) {
      g_set_error_literal (error,
                           JSON_READER_ERROR,
                           JSON_READER_ERROR_INVALID_TYPE,
                           "Expected JSON string");
      g_hash_table_unref (table);
      table = NULL;
      break;
    } else {
      g_hash_table_insert (table, g_strdup (k->data), value);
    }
  }

  g_list_free (keys);
  json_object_unref (object);

  return table;
}

GHashTable *
json_ghashtable_deserialize_data (const gchar *data,
                                  gsize length,
                                  GError **error)
{
  JsonParser *parser;
  GHashTable *table;

  if (!data) {
    return NULL;
  }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, data, length, error)) {
    g_object_unref (parser);
    return NULL;
  }

  table = json_ghashtable_deserialize (json_parser_get_root (parser),
                                       error);

  return table;
}
