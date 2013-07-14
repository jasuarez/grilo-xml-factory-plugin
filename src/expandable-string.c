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

#include "expandable-string.h"

#include "json-ghashtable.h"
#include <string.h>

typedef enum {
  UNKNOWN,
  EXPANDABLE,
  UNEXPANDABLE,
} ExpandableStatus;

struct _ExpandableString {
  gchar *str;
  ExpandableStatus status;
};

struct _ExpandData {
  guint refcount;
  gchar *source_id;
  GrlMedia *media;
  GHashTable *private_keys;
  gchar *search_text;
  GrlOperationOptions *options;
  guint max_page_size;
  GHashTable *regexp_buffers;
};

static GRegex *
expand_pattern(gboolean deinit)
{
  static GRegex *pattern = NULL;

  if (deinit) {
    if (pattern) {
      g_regex_unref (pattern);
      pattern = NULL;
    }
    return pattern;
  } else {
    if (!pattern) {
      pattern = g_regex_new ("%.*%", G_REGEX_OPTIMIZE | G_REGEX_UNGREEDY, 0, NULL);
    }
    return pattern;
  }
}

inline static gchar *
expand_data_get_search_text (ExpandData *data)
{
  if (!data) {
    return NULL;
  }

  return g_strdup (data->search_text);
}

inline static gchar *
expand_data_get_skip (ExpandData *data)
{
  if (!data) {
    return NULL;
  }

  return g_strdup_printf ("%d", grl_operation_options_get_skip (data->options));
}

inline static gchar *
expand_data_get_count (ExpandData *data)
{
  if (!data) {
    return NULL;
  }

  return g_strdup_printf ("%d", grl_operation_options_get_count (data->options));
}

static gchar *
expand_data_get_page_number (ExpandData *data)
{
  guint page_number = 0;

  if (!data) {
    return NULL;
  }

  grl_paging_translate (grl_operation_options_get_skip (data->options),
                        grl_operation_options_get_count (data->options),
                        data->max_page_size,
                        NULL,
                        &page_number,
                        NULL);

  return g_strdup_printf ("%d", page_number);
}

static gchar *
expand_data_get_page_size (ExpandData *data)
{
  guint page_size = 0;

  if (!data) {
    return NULL;
  }

  grl_paging_translate (grl_operation_options_get_skip (data->options),
                        grl_operation_options_get_count (data->options),
                        data->max_page_size,
                        &page_size,
                        NULL,
                        NULL);

  return g_strdup_printf ("%d", page_size);
}

static gchar *
expand_data_get_page_offset (ExpandData *data)
{
  guint page_offset = 0;

  if (!data) {
    return NULL;
  }

  grl_paging_translate (grl_operation_options_get_skip (data->options),
                        grl_operation_options_get_count (data->options),
                        data->max_page_size,
                        NULL,
                        NULL,
                        &page_offset);

  return g_strdup_printf ("%d", page_offset);
}

static gboolean
expand_remaining_cb (const GMatchInfo *match_info,
                     GString *result,
                     gpointer data)
{
  gchar *match;

  match = g_match_info_fetch (match_info, 0);

  /* Special case: '%%' is expanded by single '%' */
  if (strlen (match) == 2) {
    g_string_append (result, "%");
    g_free (--match);
    return TRUE;
  }
  g_free (match);

  return FALSE;
}

static gboolean
expand_metadata_key_cb (const GMatchInfo *match_info,
                        GString *result,
                        ExpandData *expand_data)
{
  GrlKeyID key;
  GrlRegistry *registry;
  const gchar *key_name = NULL;
  gchar **match_tokens;
  gchar *key_value = NULL;
  gchar *match;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  match_len = strlen (match);

  /* Remove the leading and trailing '%' character */
  match[match_len - 1] = '\0';
  match++;

  /* Check if we need to expand by metadata key value */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "key") == 0) {
    key_name = match_tokens[1];
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  /* Search for the key */
  registry = grl_registry_get_default ();
  key = grl_registry_lookup_metadata_key (registry, key_name);
  if (key == GRL_METADATA_KEY_INVALID) {
    GRL_WARNING ("Invalid key '%s'", key_name);
    g_free (--match);
    g_strfreev (match_tokens);
    return TRUE;
  }

  /* If it has not got a value, use an empty value */
  if (!expand_data ||
      !expand_data->media ||
      !grl_data_has_key (GRL_DATA (expand_data->media), key)) {
    g_free (--match);
    g_strfreev (match_tokens);
    return TRUE;
  }

  /* Convert type to string */
  switch (grl_metadata_key_get_type (key)) {
  case G_TYPE_STRING:
    key_value = g_strdup (grl_data_get_string (GRL_DATA (expand_data->media), key));
    break;
  case G_TYPE_INT:
    key_value = g_strdup_printf ("%d", grl_data_get_int (GRL_DATA (expand_data->media), key));
    break;
  case G_TYPE_FLOAT:
    key_value = g_strdup_printf ("%f", grl_data_get_float (GRL_DATA (expand_data->media), key));
    break;
  }

  g_string_append (result, key_value);

  g_free (key_value);
  g_free (--match);
  g_strfreev (match_tokens);

  return TRUE;
}

static gboolean
expand_private_cb (const GMatchInfo *match_info,
                   GString *result,
                   ExpandData *expand_data)
{
  gchar **match_tokens;
  gchar *match;
  gchar *priv_name;
  gchar *priv_value;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  match_len = strlen (match);

  /* Remove the leading and trailing '%' character */
  match[match_len - 1] = '\0';
  match++;

  /* Check if we need to expand by private value */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "priv") == 0) {
    priv_name = match_tokens[1];
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  /* Search the private value */
  if (!expand_data ||
      !expand_data->private_keys) {
    g_free (--match);
    g_strfreev (match_tokens);
    return TRUE;
  }

  /* Prepend source_id to the private name */
  priv_name = g_strconcat (expand_data->source_id, "::", priv_name, NULL);
  priv_value = g_hash_table_lookup (expand_data->private_keys, priv_name);
  g_free (priv_name);
  if (priv_value) {
    g_string_append (result, priv_value);
  }

  g_free (--match);
  g_strfreev (match_tokens);

  return TRUE;
}

static gboolean
expand_config_cb (const GMatchInfo *match_info,
                  GString *result,
                  GrlConfig *config)
{
  gchar **match_tokens;
  gchar *key_value = NULL;
  gchar *match;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  /* Remove the leading and trailing '%' character */
  match_len = strlen (match);
  match[match_len - 1] = '\0';
  match++;

  /* Check if we need to expand by config value or leave as it is */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "conf") == 0) {
    key_value = grl_config_get_string (config, match_tokens[1]);
    if (key_value) {
      g_string_append (result, key_value);
      g_free (key_value);
    } else {
      GRL_DEBUG ("No value found for %s", match);
    }
    match--;
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  g_strfreev (match_tokens);
  g_free (match);

  return TRUE;
}

static gboolean
expand_str_cb (const GMatchInfo *match_info,
               GString *result,
               GList *located_strings)
{
  GList *l;
  const gchar *str = NULL;
  gchar **match_tokens;
  gchar *match;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  /* Remove the leading and trailing '%' character */
  match_len = strlen (match);
  match[match_len - 1] = '\0';
  match++;

  /* Check if we need to expand by str value or leave as it is */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "str") == 0) {
    /* Search the located string */
    l = located_strings;
    while (l && !str) {
      str = g_hash_table_lookup (l->data, match_tokens[1]);
      l = g_list_next (l);
    }
    if (str) {
      g_string_append (result, str);
    } else {
      GRL_DEBUG ("No value found for %s", match);
    }
    match--;
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  g_strfreev (match_tokens);
  g_free (match);

  return TRUE;
}

static gboolean
expand_param_cb (const GMatchInfo *match_info,
                 GString *result,
                 ExpandData *data)
{
  gchar **match_tokens;
  gchar *match;
  gchar *param_name = NULL;
  gchar *param_value = NULL;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  /* Remove the leading and trailing '%' character */
  match_len = strlen (match);
  match[match_len -1] = '\0';
  match++;

  /* Check if we need to expand by config value or leave as it is */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "param") == 0) {
    param_name = match_tokens[1];
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  if (param_name) {
    if (g_strcmp0 (param_name, "search_text") == 0) {
      param_value = expand_data_get_search_text (data);
    } else if (g_strcmp0 (param_name, "skip") == 0) {
      param_value = expand_data_get_skip (data);
    } else if (g_strcmp0 (param_name, "count") == 0) {
      param_value = expand_data_get_count (data);
    } else if (g_strcmp0 (param_name, "page_number") == 0) {
      param_value = expand_data_get_page_number (data);
    } else if (g_strcmp0 (param_name, "page_size") == 0) {
      param_value = expand_data_get_page_size (data);
    } else if (g_strcmp0 (param_name, "page_offset") == 0) {
      param_value = expand_data_get_page_offset (data);
    } else {
      GRL_WARNING ("Invalid parameter '%s'", param_name);
    }
    match--;
  }

  g_strfreev (match_tokens);
  g_free (match);

  if (param_value) {
    g_string_append (result, param_value);
    g_free (param_value);
  }

  return TRUE;
}

static gboolean
expand_buffer_id_cb (const GMatchInfo *match_info,
                     GString *result,
                     ExpandData *data)
{
  const gchar *buffer_content;
  gchar **match_tokens;
  gchar *buffer_id;
  gchar *match;
  gint match_len;

  match = g_match_info_fetch (match_info, 0);
  /* Remove the leading and trailing '%' character */
  match_len = strlen (match);
  match[match_len -1] = '\0';
  match++;

  /* Check if we need to expand by buffer id or leave as it is */
  match_tokens = g_strsplit (match, ":", 2);
  if (g_strv_length (match_tokens) == 2 &&
      g_strcmp0 (match_tokens[0], "buf") == 0) {
    buffer_id = match_tokens[1];
  } else {
    g_free (--match);
    g_strfreev (match_tokens);
    return FALSE;
  }

  if (data && data->regexp_buffers) {
    buffer_content = g_hash_table_lookup (data->regexp_buffers, buffer_id);
    if (buffer_content) {
      g_string_append (result, buffer_content);
    }
  }

  g_free (--match);
  g_strfreev (match_tokens);

  return TRUE;
}

static gchar *
expand_full (const gchar *string,
             GRegexEvalCallback eval,
             gpointer user_data,
             ...)
{
  GMatchInfo *match_info = NULL;
  GRegexEvalCallback feval;
  GString *result;
  gint end_pos = 0;
  gint start_pos = 0;
  gint str_pos = 0;
  gint string_length;
  gpointer fdata;
  va_list vlist;

  string_length = strlen (string);
  result = g_string_sized_new (string_length);
  g_regex_match_full (expand_pattern (FALSE), string, string_length, 0, 0, &match_info, NULL);
  while (g_match_info_matches (match_info)) {
    g_match_info_fetch_pos (match_info, 0, &start_pos, &end_pos);
    g_string_append_len (result, string + str_pos, start_pos - str_pos);
    str_pos = end_pos;
    feval = eval;
    fdata = user_data;
    va_start (vlist, user_data);
    while (feval) {
      if ((*feval) (match_info, result, fdata)) {
        break;
      }
      feval = va_arg (vlist, GRegexEvalCallback);
      if (feval) {
        fdata = va_arg (vlist, gpointer);
      }
    }

    va_end (vlist);
    if (feval == NULL) {
      /* String was not expanded; put it back */
      g_string_append_len (result, string + start_pos, end_pos - start_pos);
    }
    g_match_info_next (match_info, NULL);
  }

  g_match_info_free (match_info);

  /* Add remaing string */
  g_string_append_len (result, string + str_pos, string_length - str_pos);

  return g_string_free (result, FALSE);
}

static gchar *
expand_string (const gchar *str,
               ExpandData *expand_data)
{
  gchar *expanded_str;

  expanded_str = expand_full (str,
                              (GRegexEvalCallback) expand_metadata_key_cb,
                              expand_data,
                              (GRegexEvalCallback) expand_param_cb,
                              expand_data,
                              (GRegexEvalCallback) expand_buffer_id_cb,
                              expand_data,
                              (GRegexEvalCallback) expand_private_cb,
                              expand_data,
                              (GRegexEvalCallback) expand_remaining_cb,
                              NULL,
                              NULL);

  if (!expanded_str) {
    GRL_DEBUG ("Some values cannot be expanded in '%s'", str);
  }

  return expanded_str;
}

ExpandData *
expand_data_new (GrlXmlFactorySource *source,
                 GrlMedia *media,
                 const gchar *search_text,
                 GrlOperationOptions *options)
{
  ExpandData *p;
  guint autosplit;
  static GrlKeyID GRL_METADATA_KEY_PRIVATE_KEYS = 0;
  GrlRegistry *registry;

  if (!GRL_METADATA_KEY_PRIVATE_KEYS) {
    registry = grl_registry_get_default ();
    GRL_METADATA_KEY_PRIVATE_KEYS = grl_registry_lookup_metadata_key (registry,
                                                                      "xml-factory-private-keys");
  }

  autosplit = grl_source_get_auto_split_threshold (GRL_SOURCE (source));

  p = g_slice_new (ExpandData);
  p->refcount = 1;
  g_object_get (G_OBJECT (source), "source-id", &(p->source_id), NULL);
  if (media) {
    p->media = g_object_ref (media);
    p->private_keys = json_ghashtable_deserialize_data (grl_data_get_string (GRL_DATA (media),
                                                                             GRL_METADATA_KEY_PRIVATE_KEYS),
                                                        -1,
                                                        NULL);
  } else {
    p->media = NULL;
  }
  p->search_text = g_strdup (search_text);
  p->options = g_object_ref (options);
  p->max_page_size = (autosplit <= 0)? G_MAXINT: autosplit;
  p->regexp_buffers = NULL;

  return p;
}

ExpandData *
expand_data_ref (ExpandData *data)
{
  data->refcount++;
  return data;
}

void
expand_data_unref (ExpandData *data)
{
  data->refcount--;

  if (data->refcount > 0) {
    return;
  }

  g_free (data->source_id);
  if (data->media) {
    g_object_unref (data->media);
  }
  if (data->private_keys) {
    g_hash_table_unref (data->private_keys);
  }
  g_object_unref (data->options);
  g_free (data->search_text);
  if (data->regexp_buffers) {
    g_hash_table_unref (data->regexp_buffers);
  }

  g_slice_free (ExpandData, data);
}

void
expand_data_add_buffer (ExpandData *data,
                        const gchar *buffer_id,
                        const gchar *buffer_content)
{
  if (!data->regexp_buffers) {
    data->regexp_buffers = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  NULL,
                                                  g_free);
  }

  g_hash_table_insert (data->regexp_buffers,
                       (gchar *) buffer_id,
                       g_strdup (buffer_content));
}

const gchar *expand_data_get_buffer (ExpandData *data,
                                     const gchar *buffer_id)
{
  if (!data->regexp_buffers) {
    return NULL;
  }

  return g_hash_table_lookup (data->regexp_buffers, buffer_id);
}

ExpandableString *
expandable_string_new (const gchar *init,
                       GrlConfig *config,
                       GList *located_strings)
{
  ExpandableString *exp_str;

  exp_str = g_slice_new (ExpandableString);

  if (init) {
    exp_str->str = expand_full (init,
                                (GRegexEvalCallback) expand_config_cb,
                                config,
                                (GRegexEvalCallback) expand_str_cb,
                                located_strings,
                                NULL);
  } else {
    exp_str->str = NULL;
  }

  if (exp_str->str) {
    exp_str->status = UNKNOWN;
  } else {
    exp_str->status = UNEXPANDABLE;
  }

  return exp_str;
}

void
expandable_string_free (ExpandableString *exp_str)
{
  if (exp_str) {
    g_free (exp_str->str);
    g_slice_free (ExpandableString, exp_str);
  }
}

gchar *
expandable_string_get_value (ExpandableString *exp_str,
                             ExpandData *data)
{
  gchar *expanded_string;

  if (exp_str->status == UNEXPANDABLE) {
    return exp_str->str;
  }

  expanded_string = expand_string (exp_str->str, data);

  if (exp_str->status == UNKNOWN) {
    if (g_strcmp0 (exp_str->str, expanded_string) == 0) {
      exp_str->status = UNEXPANDABLE;
    } else {
      exp_str->status = EXPANDABLE;
    }
  }

  return expanded_string;
}

void
expandable_string_free_value (ExpandableString *exp_str,
                              gchar *value)
{
  if (exp_str->str != value) {
    g_free (value);
  }
}
