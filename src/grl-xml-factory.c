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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "grl-xml-factory.h"
#include <glib.h>
#include <glib/gprintf.h>

#include "dataref.h"
#include "expandable-string.h"
#include "fetch.h"
#include "json-ghashtable.h"
#include "log.h"

#include <json-glib/json-glib.h>
#include <lauxlib.h>
#include <libxml/parser.h>
#include <libxml/xmlmemory.h>
#include <libxml/xmlschemas.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <lua.h>
#include <lualib.h>
#include <net/grl-net.h>
#include <rest/oauth-proxy.h>
#include <rest/rest-proxy.h>
#include <string.h>

#define GRL_XML_FACTORY_SOURCE_GET_PRIVATE(object)           \
  (G_TYPE_INSTANCE_GET_PRIVATE((object),                     \
                               GRL_XML_FACTORY_SOURCE_TYPE,  \
                               GrlXmlFactorySourcePrivate))

/* %TRUE if string has a non-empty value */
#define STR_HAS_VALUE(string)                   \
  ((string) && (string)[0] != '\0')

/* Executes "call(data)" in a idle if options contains GRL_RESOLVE_IDLE_RELAY
   flag; else, it invokes the call directly */
#define EXECUTE_CALL(options, call, data)                               \
  ((grl_operation_options_get_flags(options)&GRL_RESOLVE_IDLE_RELAY)? g_idle_add((GSourceFunc) (call), (data)):(call)(data))

/* ---------- Logging ---------- */

#define GRL_LOG_DOMAIN_DEFAULT xml_factory_log_domain
GRL_LOG_DOMAIN(xml_factory_log_domain);

/* ------- Pluging Info -------- */

enum {
  OP_SEARCH = 0,
  OP_BROWSE,
  OP_RESOLVE,
  OP_LAST,
};

enum {
  FORMAT_XML,
  FORMAT_JSON,
};

typedef void (*SendResultCb) (GrlMedia *media,
                              gint remaining,
                              gpointer user_data,
                              GError *error);

typedef struct _OperationRequirement {
  GrlKeyID key;
  GRegex *match_reg;
} OperationRequirement;

typedef struct _ResultData {
  guint refcount;
  FetchData *query;
  gint format;
  gint cache_time;
  gboolean cache_valid;
  union {
    DataRef *xml;
    JsonParser *json;
  } cache;
} ResultData;

typedef struct _Operation {
  glong line_number;
  gchar *id;
  ExpandableString *skip;
  ExpandableString *count;
  GrlKeyID resolve_key;
  gboolean resolve_any;
  GList *requirements;
  GType required_type;
  ResultData *result;
} Operation;

typedef struct _NameSpace {
  xmlChar *prefix;
  xmlChar *href;
} NameSpace;

typedef struct _PrivateData {
  gchar *name;
  ExpandableString *data;
} PrivateData;

typedef struct _MediaTemplate {
  glong line_number;
  gchar *operation_id;
  GType media_type;
  gint format;
  NameSpace *namespace;
  gint namespace_size;
  ExpandableString *query;
  ExpandableString *select;
  GHashTable *keys;
  GList *mandatory_keys;
  GList *private_keys;
} MediaTemplate;

typedef struct _GetRawData {
  DataRef *xpath_reffed;
  DataRef *xml_ctx_reffed;
  DataRef *xml_doc_reffed;
  gint node;
  NameSpace *namespace;
  gint namespace_size;
  JsonArray *json_array;
  ExpandData *expand_data;
} GetRawData;

typedef struct _OperationCallData {
  GrlXmlFactorySource *source;
  Operation *operation;
  gint operation_type;
  SendResultCb callback;
  GCancellable *cancellable;
  GList *keys;
  GrlOperationOptions *options;
  DataRef *xml_doc_reffed;
  JsonParser *json_parser;
  ExpandData *expand_data;
  guint skip;
  guint count;
  GList *send_list;
  gint total_results;
  gpointer user_data;
} OperationCallData;

typedef struct _SendItem {
  GrlMedia *media;
  gint pending_count;
  gboolean apply_resolve;
} SendItem;

typedef struct _FetchItemData {
  OperationCallData *op_data;
  SendItem *item;
  GrlKeyID key;
} FetchItemData;

struct _GrlXmlFactorySourcePrivate {
  GHashTable *results;
  GList *rest_proxy_list;
  GList *operations[OP_LAST];
  GList *supported_keys;
  GList *slow_keys;
  GList *use_resolve_keys;
  GList *media_templates;
  GList *located_strings;
  GrlConfig *config;
  GrlNetWc *wc;
  gchar *user_agent;
  GrlXmlDebug debug;
  gint autosplit;
  GrlKeyID private_keys_key;
  lua_State *lua_state;
};

gboolean grl_xml_factory_plugin_init (GrlRegistry *registry,
                                      GrlPlugin *plugin,
                                      GList *configs);

static GrlXmlFactorySource *grl_xml_factory_source_new (const gchar *xml_spec_path,
                                                        xmlSchemaPtr schema,
                                                        gint min_version,
                                                        gint max_version,
                                                        GList *configs);

static const GList *grl_xml_factory_source_supported_keys (GrlSource *source);

static const GList *grl_xml_factory_source_slow_keys (GrlSource *source);


static GrlSupportedOps grl_xml_factory_source_supported_operations (GrlSource *source);

static void grl_xml_factory_source_browse (GrlSource *source,
                                           GrlSourceBrowseSpec *bs);

static void grl_xml_factory_source_resolve (GrlSource *source,
                                            GrlSourceResolveSpec *rs);

static gboolean grl_xml_factory_source_may_resolve (GrlSource *source,
                                                    GrlMedia *media,
                                                    GrlKeyID key_id,
                                                    GList **missing_keys);

static void grl_xml_factory_source_search (GrlSource *source,
                                           GrlSourceSearchSpec *ss);

static void grl_xml_factory_source_cancel (GrlSource *source,
                                           guint operation_id);

static void operation_free (Operation *operation);

static void media_template_free (MediaTemplate *template);

static void merge_config (GList *options,
                          GrlConfig *into,
                          GrlConfig *from);

static GrlConfig *merge_all_configs (const gchar *source_id,
                                     GList *options,
                                     GList *available_configs,
                                     GrlConfig *default_config);

static gboolean all_options_have_value (GList *options,
                                        GrlConfig *config);

static OperationCallData * get_resolve_data (GrlXmlFactorySource *source,
                                             GCancellable *cancellable,
                                             GrlMedia *media,
                                             GList *keys,
                                             GrlOperationOptions *options,
                                             SendResultCb callback,
                                             gpointer user_data);

static void operation_call_send_list_run (OperationCallData *data);

static void operation_call (OperationCallData *data);

static xmlSchemaPtr get_xml_schema (void);

static GList *get_source_xml_specs (void);

static xmlNodePtr xml_get_node (xmlNodePtr xml_node);

static gboolean xml_get_property_boolean (xmlNodePtr xml_node,
                                          const xmlChar *property);

static FetchData *xml_spec_get_fetch_data (GrlXmlFactorySource *source,
                                           xmlNodePtr xml_node);

static gboolean xml_spec_key_is_supported (GrlKeyID key);

static void xml_spec_get_basic_info (xmlNodePtr xml_node,
                                     gchar **id,
                                     gchar **name,
                                     gchar **description,
                                     gchar **icon,
                                     xmlNodePtr *strings,
                                     xmlNodePtr *config,
                                     xmlNodePtr *script,
                                     xmlNodePtr *operation,
                                     xmlNodePtr *provide);

static GrlConfig *xml_spec_get_config (xmlNodePtr xml_config_node,
                                       const gchar *source_id,
                                       GList **config_keys);

static GList* xml_spec_get_strings (xmlNodePtr xml_node);

static lua_State *xml_spec_get_init_script (xmlNodePtr xml_node,
                                            GrlConfig *config,
                                            GList *strings);

static MediaTemplate *xml_spec_get_provide_media_template (GrlXmlFactorySource *source,
                                                           xmlNodePtr xml_node,
                                                           GHashTable *required_keys);

static void xml_spec_get_operation (GrlXmlFactorySource *source,
                                    xmlNodePtr xml_node);

void handle_schema_errors (void *ctx,
                           const gchar *msg,
                           ...);

void handle_schema_warnings (void *ctx,
                             const gchar *msg,
                             ...);

GrlKeyID GRL_METADATA_KEY_PRIVATE_KEYS = 0;

/* =================== XML Factory Plugin  =============== */


gboolean
grl_xml_factory_plugin_init (GrlRegistry *registry,
                             GrlPlugin *plugin,
                             GList *configs)
{
  GError *error = NULL;
  GList *source_xml_specs;
  GList *spec;
  GParamSpec *pspec;
  GrlXmlFactorySource *source;
  const gchar *supported_version;
  gboolean source_loaded = FALSE;
  gchar **supported_versions;
  gint back_supported_version;
  gint max_supported_version;
  xmlSchemaPtr source_schema;

  GRL_LOG_DOMAIN_INIT (xml_factory_log_domain, "xml-factory");

  /* Register key to store private values */
  pspec = g_param_spec_string ("xml-factory-private-keys",
                               "XML Factory Private Keys",
                               "XML Factory Private keys",
                               NULL,
                               G_PARAM_READWRITE);

  GRL_METADATA_KEY_PRIVATE_KEYS = grl_registry_register_metadata_key (registry, pspec, &error);
  if (!GRL_METADATA_KEY_PRIVATE_KEYS) {
    GRL_WARNING ("Can't register \"xml-factory-private-keys\" key: %s", error->message);
    g_error_free (error);

    return FALSE;
  }

  source_xml_specs = get_source_xml_specs ();
  source_schema = get_xml_schema ();

  supported_version = grl_plugin_get_info (plugin, "api_version");
  if (!supported_version) {
    GRL_WARNING ("Supported API version not defined");
    return FALSE;
  }

  supported_versions = g_strsplit (supported_version, ".", 2);
  if (g_strv_length (supported_versions) != 2) {
    GRL_WARNING ("Wrong supported API version '%s'", supported_version);
    g_strfreev (supported_versions);
    return FALSE;
  }

  max_supported_version =
    (gint) g_ascii_strtoll ((const gchar *) supported_versions[0], NULL, 10);
  back_supported_version =
    (gint) g_ascii_strtoull ((const gchar *) supported_versions[1], NULL, 10);

  g_strfreev (supported_versions);

  if (!source_xml_specs) {
    xmlSchemaFree (source_schema);
    return TRUE;
  }

  for (spec = source_xml_specs; spec; spec = g_list_next (spec)) {
    source = grl_xml_factory_source_new (spec->data,
                                         source_schema,
                                         max_supported_version - back_supported_version,
                                         max_supported_version,
                                         configs);
    if (source) {
      source_loaded = TRUE;
      grl_registry_register_source (registry, plugin, GRL_SOURCE (source), NULL);
    }
  }

  g_list_free_full (source_xml_specs, g_free);
  xmlSchemaFree (source_schema);

  return source_loaded;
}

GRL_PLUGIN_REGISTER (grl_xml_factory_plugin_init,
                     NULL,
                     XML_FACTORY_PLUGIN_ID);

G_DEFINE_TYPE (GrlXmlFactorySource,
               grl_xml_factory_source,
               GRL_TYPE_SOURCE);

static void
grl_xml_factory_source_finalize (GObject *object)
{
  GrlXmlFactorySource *self = GRL_XML_FACTORY_SOURCE (object);

  g_list_free (self->priv->supported_keys);
  g_list_free (self->priv->slow_keys);
  g_list_free (self->priv->use_resolve_keys);
  g_list_free_full (self->priv->media_templates,
                    (GDestroyNotify) media_template_free);

  g_list_free_full (self->priv->operations[OP_SEARCH], (GDestroyNotify) operation_free);
  g_list_free_full (self->priv->operations[OP_BROWSE], (GDestroyNotify) operation_free);
  g_list_free_full (self->priv->operations[OP_RESOLVE], (GDestroyNotify) operation_free);

  g_list_free_full (self->priv->located_strings, (GDestroyNotify) g_hash_table_unref);

  if (self->priv->user_agent) {
    g_free (self->priv->user_agent);
  }

  if (self->priv->results) {
    g_hash_table_unref (self->priv->results);
  }

  if (self->priv->lua_state) {
    lua_close (self->priv->lua_state);
  }

  G_OBJECT_CLASS (grl_xml_factory_source_parent_class)->finalize (object);
}

static void
grl_xml_factory_source_dispose (GObject *object)
{
  GrlXmlFactorySource *self = GRL_XML_FACTORY_SOURCE (object);

  if (self->priv->config) {
    g_object_unref (self->priv->config);
    self->priv->config = NULL;
  }

  if (self->priv->wc) {
    g_object_unref (self->priv->wc);
    self->priv->wc = NULL;
  }

  if (self->priv->rest_proxy_list) {
    g_list_free_full (self->priv->rest_proxy_list, g_object_unref);
    self->priv->rest_proxy_list = NULL;
  }

  G_OBJECT_CLASS (grl_xml_factory_source_parent_class)->dispose (object);
}

static void
grl_xml_factory_source_class_init (GrlXmlFactorySourceClass * klass)
{
  GObjectClass *g_class = G_OBJECT_CLASS (klass);
  GrlSourceClass *source_class = GRL_SOURCE_CLASS (klass);

  g_class->finalize = grl_xml_factory_source_finalize;
  g_class->dispose = grl_xml_factory_source_dispose;

  source_class->supported_keys = grl_xml_factory_source_supported_keys;
  source_class->slow_keys = grl_xml_factory_source_slow_keys;
  source_class->supported_operations = grl_xml_factory_source_supported_operations;
  source_class->cancel = grl_xml_factory_source_cancel;
  source_class->search = grl_xml_factory_source_search;
  source_class->browse = grl_xml_factory_source_browse;
  source_class->resolve = grl_xml_factory_source_resolve;
  source_class->may_resolve = grl_xml_factory_source_may_resolve;

  g_type_class_add_private (klass, sizeof (GrlXmlFactorySourcePrivate));
}

static void
grl_xml_factory_source_init (GrlXmlFactorySource *source)
{
  source->priv = GRL_XML_FACTORY_SOURCE_GET_PRIVATE (source);

  source->priv->wc = grl_net_wc_new ();
}

static GrlXmlFactorySource *
grl_xml_factory_source_new (const gchar *xml_spec_path,
                            xmlSchemaPtr schema,
                            gint min_version,
                            gint max_version,
                            GList *configs)
{
  ExpandableString *expandable_source_description;
  ExpandableString *expandable_source_icon;
  ExpandableString *expandable_source_name;
  GFile *file;
  GHashTable *required_keys;
  GIcon *icon = NULL;
  GList *config_keys = NULL;
  GList *located_strings = NULL;
  GList *operation;
  GList *requirement;
  GrlConfig *default_config;
  GrlConfig *merged_config;
  GrlMediaType supported_media_type = GRL_MEDIA_TYPE_NONE;
  GrlXmlFactorySource *source;
  MediaTemplate *template;
  gboolean debug;
  gboolean supported_api_version = TRUE;
  gchar *expanded_source_description;
  gchar *expanded_source_icon;
  gchar *expanded_source_name;
  gchar *source_description = NULL;
  gchar *source_icon = NULL;
  gchar *source_id = NULL;
  gchar *source_name = NULL;
  gchar *user_agent;
  gint api_version;
  gint autosplit = 0;
  gint i;
  lua_State *lua_state = NULL;
  xmlChar *api_version_str;
  xmlChar *autosplit_str;
  xmlDocPtr xml_doc;
  xmlNodePtr xml_config = NULL;
  xmlNodePtr xml_node;
  xmlNodePtr xml_operation = NULL;
  xmlNodePtr xml_provide = NULL;
  xmlNodePtr xml_script = NULL;
  xmlNodePtr xml_strings = NULL;
  xmlSchemaValidCtxtPtr validate_ctx;

  xml_doc = xmlReadFile (xml_spec_path, NULL,
                         XML_PARSE_RECOVER |
                         XML_PARSE_NOBLANKS |
                         XML_PARSE_NOWARNING |
                         XML_PARSE_NOERROR);

  if (!xml_doc) {
    GRL_WARNING ("Unable to read %s", xml_spec_path);
    return NULL;
  }

  /* Validate the XML */
  validate_ctx = xmlSchemaNewValidCtxt (schema);
  if (!validate_ctx) {
    GRL_WARNING ("Can not create XML schema validation context");
    return NULL;
  }

  xmlSchemaSetValidErrors (validate_ctx,
                           handle_schema_errors,
                           handle_schema_warnings,
                           NULL);
  if (xmlSchemaValidateDoc (validate_ctx, xml_doc) != 0) {
    GRL_DEBUG ("'%s' source specification is invalid", xml_spec_path);
    xmlFreeDoc (xml_doc);
    xmlSchemaFreeValidCtxt (validate_ctx);
    return NULL;
  }

  xmlSchemaFreeValidCtxt (validate_ctx);

  xml_node = xmlDocGetRootElement (xml_doc);

  /* Get the implemented API version */
  api_version_str = xmlGetProp (xml_node, (const xmlChar *) "api");
  if (STR_HAS_VALUE (api_version_str)) {
    api_version = (gint) g_ascii_strtoll ((const gchar *) api_version_str, NULL, 10);
    if (api_version < min_version || api_version > max_version) {
      supported_api_version = FALSE;
    }
  } else {
    supported_api_version = FALSE;
  }

  if (!supported_api_version) {
      GRL_DEBUG ("Implemented API version is '%s', required is "
                 "between '%d' and '%d'; skipping '%s'",
                 (const gchar *) api_version_str,
                 min_version,
                 max_version,
                 xml_spec_path);
      g_free (api_version_str);
      xmlFreeDoc (xml_doc);
      return NULL;
  }
  g_free (api_version_str);

  /* Get autosplit threshold value, if defined */
  autosplit_str = xmlGetProp (xml_node, (const xmlChar *) "autosplit");
  if (STR_HAS_VALUE (autosplit_str)) {
    autosplit = (gint) g_ascii_strtoll ((const gchar *) autosplit_str, NULL, 10);
  }
  xmlFree (autosplit_str);

  /* Get user-agent property, if defined */
  user_agent = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "user-agent");
  if (!STR_HAS_VALUE (user_agent)) {
    g_free (user_agent);
    user_agent = NULL;
  }

  /* Get source basic information */
  xml_node = xml_get_node (xml_node->children);
  xml_spec_get_basic_info (xml_node, &source_id, &source_name,
                           &source_description, &source_icon,
                           &xml_strings, &xml_config, &xml_script,
                           &xml_operation, &xml_provide);

  /* Check config */
  default_config = xml_spec_get_config (xml_config, source_id, &config_keys);

  /* Merge all configs for this source */
  merged_config = merge_all_configs (source_id, config_keys, configs, default_config);
  g_object_unref (default_config);

  if (!all_options_have_value (config_keys, merged_config)) {
    GRL_DEBUG ("Some options do not have a value; skipping source '%s'", source_id);
    g_object_unref (merged_config);
    g_list_free_full (config_keys, g_free);
    g_free (source_id);
    g_free (source_name);
    g_free (source_description);
    g_free (source_icon);
    xmlFreeDoc (xml_doc);
    return NULL;
  }

  /* Get located strings */
  if (xml_strings) {
    located_strings = xml_spec_get_strings (xml_strings);
  }

  /* Initialize script */
  if (xml_script) {
    lua_state = xml_spec_get_init_script (xml_script, merged_config, located_strings);
    if (!lua_state) {
      GRL_DEBUG ("Script initialization has failed; skipping source '%s'", source_id);
      g_object_unref (merged_config);
      g_list_free_full (config_keys, g_free);
      g_free (source_id);
      g_free (source_name);
      g_free (source_description);
      g_free (source_icon);
      xmlFreeDoc (xml_doc);
      return NULL;
    }
  }

  /* Expand source name, description and icon */
  expandable_source_name = expandable_string_new (source_name,
                                                  merged_config,
                                                  located_strings);
  expanded_source_name = expandable_string_get_value (expandable_source_name, NULL);

  expandable_source_description = expandable_string_new (source_description,
                                                         merged_config,
                                                         located_strings);
  expanded_source_description = expandable_string_get_value (expandable_source_description, NULL);

  expandable_source_icon = expandable_string_new (source_icon,
                                                  merged_config,
                                                  located_strings);
  expanded_source_icon = expandable_string_get_value (expandable_source_icon, NULL);

  if (expanded_source_icon) {
    file = g_file_new_for_uri (expanded_source_icon);
    icon = g_file_icon_new (file);
    g_object_unref (file);
  }

  source = g_object_new (GRL_XML_FACTORY_SOURCE_TYPE,
                         "source-id", source_id,
                         "source-name", expanded_source_name,
                         "source-desc", expanded_source_description,
                         "source-icon", icon,
                         NULL);

  g_clear_object (&icon);
  expandable_string_free_value (expandable_source_name, expanded_source_name);
  expandable_string_free_value (expandable_source_description, expanded_source_description);
  expandable_string_free_value (expandable_source_icon, expanded_source_icon);
  expandable_string_free (expandable_source_name);
  expandable_string_free (expandable_source_description);
  expandable_string_free (expandable_source_icon);

  source->priv->config = merged_config;
  source->priv->located_strings = located_strings;
  source->priv->lua_state = lua_state;

  if (user_agent) {
    source->priv->user_agent = user_agent;
    g_object_set (G_OBJECT (source->priv->wc), "user-agent", user_agent, NULL);
  }

  g_free (source_id);
  g_free (source_name);
  g_free (source_description);
  g_list_free_full (config_keys, g_free);

  /* Check supported operations */
  debug = xml_get_property_boolean (xml_operation, (const xmlChar *) "debug");
  if (debug) {
    source->priv->debug |= GRL_XML_DEBUG_OPERATION;
  }
  xml_operation = xml_get_node (xml_operation->children);
  while (xml_operation) {
    xml_spec_get_operation (source, xml_operation);
    xml_operation = xml_get_node (xml_operation->next);
  }


  /* Get all the keys involved in requirements to make them "force" keys.
     Include also "source" and "id" as forced (use a set)*/
  required_keys = g_hash_table_new (NULL, NULL);

  g_hash_table_replace (required_keys,
                        GRLKEYID_TO_POINTER (GRL_METADATA_KEY_SOURCE),
                        GRLKEYID_TO_POINTER (GRL_METADATA_KEY_SOURCE));

  g_hash_table_replace (required_keys,
                        GRLKEYID_TO_POINTER (GRL_METADATA_KEY_ID),
                        GRLKEYID_TO_POINTER (GRL_METADATA_KEY_ID));

  for (i = 0; i < OP_LAST; i++) {
    for (operation = source->priv->operations[i];
         operation;
         operation = g_list_next (operation)) {
      for (requirement = ((Operation *) operation->data)->requirements;
           requirement;
           requirement = g_list_next (requirement)) {
        g_hash_table_replace (required_keys,
                              GRLKEYID_TO_POINTER(((OperationRequirement *) requirement->data)->key),
                              GRLKEYID_TO_POINTER(((OperationRequirement *) requirement->data)->key));
      }
    }
  }

  /* Check provided content */
  debug = xml_get_property_boolean (xml_provide, (const xmlChar *) "debug");
  if (debug) {
    source->priv->debug |= GRL_XML_DEBUG_PROVIDE;
  }
  xml_provide = xml_get_node (xml_provide->children);
  while (xml_provide) {
    template = xml_spec_get_provide_media_template (source, xml_provide, required_keys);
    if (template) {
      source->priv->media_templates =
        g_list_prepend (source->priv->media_templates, template);
      if (template->media_type == GRL_TYPE_MEDIA_AUDIO) {
        supported_media_type |= GRL_MEDIA_TYPE_AUDIO;
      } else if (template->media_type == GRL_TYPE_MEDIA_VIDEO) {
        supported_media_type |= GRL_MEDIA_TYPE_VIDEO;
      } else if (template->media_type == GRL_TYPE_MEDIA_IMAGE) {
        supported_media_type |= GRL_MEDIA_TYPE_IMAGE;
      } else if (template->media_type == GRL_TYPE_MEDIA) {
        supported_media_type |= GRL_MEDIA_TYPE_ALL;
      }
    }
    xml_provide = xml_get_node (xml_provide->next);
  }
  source->priv->media_templates = g_list_reverse (source->priv->media_templates);

  g_hash_table_unref (required_keys);

  g_object_set (G_OBJECT (source), "supported-media", supported_media_type, NULL);

  /* Set the autosplit threshold */
  if (autosplit > 0) {
    grl_source_set_auto_split_threshold (GRL_SOURCE (source),
                                         autosplit);
  }

  xmlFreeDoc (xml_doc);

  return source;
}

/* ======================= Utilities ==================== */

/* Show errors in schema validation */
void handle_schema_errors (void *ctx,
                           const gchar *msg,
                           ...)
{
  va_list args;
  gchar *error_msg;

  va_start (args, msg);
  g_vasprintf (&error_msg, msg, args);
  GRL_DEBUG (error_msg);
  g_free (error_msg);
  va_end (args);
}

void handle_schema_warnings (void *ctx,
                             const gchar *msg,
                             ...)
{
  va_list args;
  gchar *error_msg;

  va_start (args, msg);
  g_vasprintf (&error_msg, msg, args);
  GRL_DEBUG (error_msg);
  g_free (error_msg);
  va_end (args);
}

/* Pretty-print GrlMedia based @t */
static const gchar *
gtype_to_string (GType t)
{
  if (t == GRL_TYPE_MEDIA_AUDIO) {
    return "audio";
  } else if (t == GRL_TYPE_MEDIA_VIDEO) {
    return "video";
  } else if (t == GRL_TYPE_MEDIA_IMAGE) {
    return "image";
  } else if (t == GRL_TYPE_MEDIA_BOX) {
    return "box";
  } else {
    return "unknown";
  }
}

/* Inserts @value into @media, taking care of converting to proper type */
static void
insert_value (GrlXmlFactorySource *source,
              GrlMedia *media,
              GrlKeyID key,
              const gchar *value)
{
  GDateTime *datetime;
  GType key_type;

  GRL_XML_DEBUG (source,
                 GRL_XML_DEBUG_PROVIDE,
                 "Adding \"%s\" key: \"%s\"",
                 grl_metadata_key_get_name (key),
                 value);

  key_type = grl_metadata_key_get_type (key);
  if (key_type == G_TYPE_STRING) {
    grl_data_set_string (GRL_DATA (media),
                         key,
                         (const gchar *) value);
  } else if (key_type == G_TYPE_INT) {
    grl_data_set_int (GRL_DATA (media),
                      key,
                      (gint) g_ascii_strtoll ((const gchar *) value, NULL, 10));
  } else if (key_type == G_TYPE_FLOAT) {
    grl_data_set_float (GRL_DATA (media),
                        key,
                        (gfloat) g_ascii_strtod ((const gchar *) value, NULL));
  } else if (key_type == G_TYPE_DATE_TIME) {
    datetime = grl_date_time_from_iso8601 (value);
    grl_data_set_boxed (GRL_DATA (media), key, datetime);
    g_date_time_unref (datetime);
   }
}

inline static OperationRequirement *
operation_requirement_new (void)
{
  return g_slice_new (OperationRequirement);
}

static void
operation_requirement_free (OperationRequirement *req)
{
  if (req->match_reg) {
    g_regex_unref (req->match_reg);
  }
  g_slice_free (OperationRequirement, req);
}

inline static SendItem*
send_item_new (void)
{
  return g_slice_new0 (SendItem);
};

inline static void
send_item_free (SendItem *item)
{
  g_slice_free (SendItem, item);
}

inline static FetchItemData *
fetch_item_data_new (void)
{
  return g_slice_new (FetchItemData);
}

inline static void
fetch_item_data_free (FetchItemData *data)
{
  g_slice_free (FetchItemData, data);
}

inline static ResultData *
result_data_new (void)
{
  ResultData *data = g_slice_new0 (ResultData);
  data->refcount = 1;

  return data;
}

static ResultData *
result_data_ref (ResultData *data)
{
  data->refcount++;
  return data;
}

static void
result_data_unref (ResultData *data)
{
  data->refcount--;
  if (data->refcount == 0) {
    if (data->query) {
      fetch_data_free (data->query);
    }
    if (data->cache.xml) {
      (data->format == FORMAT_XML)? dataref_unref (data->cache.xml): g_object_unref (data->cache.json);
    }
    g_slice_free (ResultData, data);
  }
}

static Operation *
operation_new (void)
{
  Operation *operation;

  operation = g_slice_new0 (Operation);
  operation->required_type = GRL_TYPE_MEDIA;

  return operation;
}

static void
operation_free (Operation *operation)
{
  g_free (operation->id);
  expandable_string_free (operation->count);
  expandable_string_free (operation->skip);
  g_list_free_full (operation->requirements, (GDestroyNotify) operation_requirement_free);
  if (operation->result) {
    result_data_unref (operation->result);
  }

  g_slice_free (Operation, operation);
}

inline static PrivateData *
private_data_new (void)
{
  return g_slice_new0 (PrivateData);
}

static void
private_data_free (PrivateData *data)
{
  g_free (data->name);
  expandable_string_free (data->data);
  g_slice_free (PrivateData, data);
}

static MediaTemplate *
media_template_new (void)
{
  MediaTemplate *template;

  template = g_slice_new0 (MediaTemplate);
  template->keys = g_hash_table_new_full ((GHashFunc) g_direct_hash,
                                          (GEqualFunc) g_direct_equal,
                                          NULL,
                                          (GDestroyNotify) fetch_data_free);

  return template;
}

static void
media_template_free (MediaTemplate *template)
{
  int i;

  g_free (template->operation_id);
  if (template->namespace) {
    for (i = 0; i < template->namespace_size; i++) {
      xmlFree (template->namespace[i].prefix);
      xmlFree (template->namespace[i].href);
    }
    g_free (template->namespace);
  }

  expandable_string_free (template->query);
  expandable_string_free (template->select);
  g_hash_table_unref (template->keys);
  g_list_free (template->mandatory_keys);
  g_list_free_full (template->private_keys, (GDestroyNotify) private_data_free);

  g_slice_free (MediaTemplate, template);
}

static inline GetRawData *
get_raw_data_new (void)
{
  return g_slice_new0 (GetRawData);
}

static void
get_raw_data_free (GetRawData *data)
{
  if (data->xml_doc_reffed) {
    dataref_unref (data->xpath_reffed);
    dataref_unref (data->xml_ctx_reffed);
    dataref_unref (data->xml_doc_reffed);
  }
  if (data->json_array) {
    json_array_unref (data->json_array);
  }

  expand_data_unref (data->expand_data);
  g_slice_free (GetRawData, data);
}

inline static OperationCallData *
operation_call_data_new (void)
{
  return g_slice_new0 (OperationCallData);
}

inline static void
operation_call_data_free (OperationCallData *data)
{
  g_clear_pointer (&data->xml_doc_reffed, (GDestroyNotify) dataref_unref);
  g_clear_pointer (&data->expand_data, (GDestroyNotify) expand_data_unref);
  g_clear_object (&data->cancellable);

  g_slice_free (OperationCallData, data);
}

/* Creates a new list containing the elements in list1 + list2, dropping the
   duplicates ones; use #g_list_free() when done */
static GList *
merge_lists (GList *list1,
             GList *list2)
{
  GList *e;
  GList *l[] = { list1, list2 };
  GList *merged_list = NULL;
  gint i;

  for (i = 0; i < G_N_ELEMENTS (l); i++) {
    for (e = l[i]; e; e = g_list_next (e)) {
      if (!g_list_find (merged_list, e->data)) {
        merged_list = g_list_prepend (merged_list, e->data);
      }
    }
  }

  return merged_list;
}

/* Insert in @into all the @options in @from not already present */
static void
merge_config (GList *options,
              GrlConfig *into,
              GrlConfig *from)
{
  gchar *value;

  if (!from) {
    return;
  }

  while (options) {
    if (!grl_config_has_param (into, options->data) &&
        grl_config_has_param (from, options->data)) {
      value = grl_config_get_string (from, options->data);
      grl_config_set_string (into, options->data, value);
      g_free (value);
    }
    options = g_list_next (options);
  }
}

/* Merge @options for the @source_id. The order is to use first the options
   specific for the source, then the remaining options for any source in the
   plugin, and finally the options from default config */
static GrlConfig *
merge_all_configs (const gchar *source_id,
                   GList *options,
                   GList *available_configs,
                   GrlConfig *default_config)
{
  GList *all_configs;
  GList *config;
  GList *generic_configs = NULL;
  GList *specific_configs = NULL;
  GrlConfig *merged_config;
  gchar *config_source_id;

  while (available_configs) {
    config_source_id = grl_config_get_source (available_configs->data);
    if (config_source_id == NULL) {
      generic_configs = g_list_prepend (generic_configs, available_configs->data);
    } else if (g_strcmp0 (config_source_id, source_id) == 0) {
      specific_configs = g_list_prepend (specific_configs, available_configs->data);
    }
    g_free (config_source_id);
    available_configs = g_list_next (available_configs);
  }

  all_configs = g_list_concat (g_list_reverse (specific_configs),
                               g_list_reverse (generic_configs));

  all_configs = g_list_append (all_configs, default_config);

  /* Now merge them */
  merged_config = grl_config_new (XML_FACTORY_PLUGIN_ID, source_id);
  for (config = all_configs; config; config = g_list_next (config)) {
    merge_config (options, merged_config, config->data);
  }

  g_list_free (all_configs);

  return merged_config;
}

static gboolean
all_options_have_value (GList *options,
                        GrlConfig *config)
{
  while (options) {
    if (!grl_config_has_param (config, options->data)) {
      return FALSE;
    }
    options = g_list_next (options);
  }

  return TRUE;
}

static RestProxy *
find_rest_proxy (GList *proxy_list,
                 const gchar *endpoint,
                 gboolean oauth)
{
  gchar *url;

  while (proxy_list) {
    if (OAUTH_IS_PROXY (proxy_list->data) == oauth) {
      g_object_get (G_OBJECT (proxy_list->data),
                    "url-format", &url,
                    NULL);
      if (g_strcmp0 (url, endpoint) == 0) {
        g_free (url);
        return proxy_list->data;
      }
      g_free (url);
    }

    proxy_list = g_list_next (proxy_list);
  }

  return NULL;
}

static void
merge_hashtables (GHashTable *t1,
                  GHashTable *t2)
{
  GHashTableIter iter;
  gchar *key;
  gchar *value;

  g_hash_table_iter_init (&iter, t2);
  while (g_hash_table_iter_next (&iter, (gpointer *) &key, (gpointer * )&value)) {
    if (g_hash_table_lookup (t1, key) == NULL) {
      g_hash_table_insert (t1, g_strdup (key), g_strdup (value));
    }
  }
}

static xmlSchemaPtr
get_xml_schema ()
{
  const gchar *envvar;
  const gchar *schema_path;
  xmlSchemaParserCtxtPtr schema_ctx;
  xmlSchemaPtr schema = NULL;

  envvar = g_getenv ("GRL_XML_FACTORY_SCHEMA");
  if (envvar != NULL) {
    schema_path = envvar;
  } else {
    schema_path = XML_FACTORY_SCHEMA_LOCATION "/" XML_FACTORY_PLUGIN_ID ".xsd";
  }

  schema_ctx = xmlSchemaNewParserCtxt (schema_path);
  if (!schema_ctx) {
    GRL_WARNING ("Can not read XML schema validator (%s)", schema_path);
    return NULL;
  }

  schema = xmlSchemaParse (schema_ctx);
  if (!schema) {
    GRL_WARNING ("Error parsing XML schema (%s)", schema_path);
  }

  xmlSchemaFreeParserCtxt (schema_ctx);

  return schema;
}

/* Get a list of XML specification fullname paths */
static GList *
get_source_xml_specs ()
{
  GDir *dir;
  GList *each_path;
  GList *xml_location_path_list = NULL;
  GList *xml_specs = NULL;
  const gchar * const *system_dir;
  const gchar *envvar;
  const gchar *xml_file;

  envvar = g_getenv ("GRL_XML_FACTORY_SPECS_PATH");

  if (envvar != NULL) {
    /* Add environment-defined place  */
    xml_location_path_list =
      g_list_prepend (xml_location_path_list, g_strdup (envvar));

  } else {
    /* Add global-defined places */
    for (system_dir = g_get_system_data_dirs (); *system_dir; system_dir++) {
      xml_location_path_list =
        g_list_prepend (xml_location_path_list,
                        g_strconcat (*system_dir,
                                     G_DIR_SEPARATOR_S,
                                     XML_FACTORY_SOURCE_LOCATION,
                                     NULL));
    }

    /* Add user-defined place  */
    xml_location_path_list =
      g_list_prepend (xml_location_path_list,
                      g_strconcat (g_get_user_data_dir (),
                                   G_DIR_SEPARATOR_S,
                                   XML_FACTORY_SOURCE_LOCATION,
                                   NULL));
  }

  /* Now, get the XML files */
  for (each_path = xml_location_path_list; each_path; each_path = g_list_next (each_path)) {
    dir = g_dir_open (each_path->data, 0, NULL);
    if (!dir) {
      continue;
    }

    while ((xml_file = g_dir_read_name (dir))) {
      if (g_str_has_suffix (xml_file, ".xml")) {
        xml_specs = g_list_prepend (xml_specs,
                                    g_strconcat (each_path->data,
                                                 G_DIR_SEPARATOR_S,
                                                 xml_file,
                                                 NULL));
      }
    }

    g_dir_close (dir);
  }

  g_list_free_full (xml_location_path_list, g_free);

  return g_list_reverse (xml_specs);
}

static gboolean
cache_expired_cb (ResultData *result)
{
  result->cache_valid = FALSE;

  return FALSE;
}

/* Returns the first node, skipping all the comments */
static xmlNodePtr
xml_get_node (xmlNodePtr xml_node)
{
  while (xml_node &&
         xml_node->type == XML_COMMENT_NODE) {
    xml_node = xml_node->next;
  }

  return xml_node;
}

/* Returns %TRUE if @property exists in @xml_node and has value "1" or "true" */
static gboolean
xml_get_property_boolean (xmlNodePtr xml_node,
                          const xmlChar *property)
{
  gboolean bool_val;
  xmlChar *prop_val;

  prop_val = xmlGetProp (xml_node, property);

  if (STR_HAS_VALUE (prop_val) &&
      (xmlStrcmp (prop_val, (const xmlChar *) "1") == 0 ||
       xmlStrcmp (prop_val, (const xmlChar *) "true") == 0)) {
    bool_val = TRUE;
  } else {
    bool_val = FALSE;
  }

  xmlFree (prop_val);

  return bool_val;
}

/* Check if %key is supported; right now, only numbers and strings are
   supported */
static gboolean
xml_spec_key_is_supported (GrlKeyID key)
{
  GType key_type = grl_metadata_key_get_type (key);

  if (key_type == G_TYPE_STRING ||
      key_type == G_TYPE_INT ||
      key_type == G_TYPE_FLOAT ||
      key_type == G_TYPE_DATE_TIME) {
    return TRUE;
  } else {
    return FALSE;
  }
}

static ExpandableString *
xml_spec_get_expandable_string_impl (xmlNodePtr xml_node,
                                     GrlConfig *config,
                                     GList *located_strings)
{
  ExpandableString *exp_str = NULL;
  xmlChar *string;

  string = xmlNodeGetContent (xml_node);
  if (STR_HAS_VALUE (string)) {
    exp_str = expandable_string_new ((const gchar *) string,
                                     config,
                                     located_strings);
  }
  xmlFree (string);

  return exp_str;
}

static ExpandableString *
xml_spec_get_expandable_string (GrlXmlFactorySource *source,
                                xmlNodePtr xml_node)
{
  return xml_spec_get_expandable_string_impl (xml_node,
                                              source->priv->config,
                                              source->priv->located_strings);
}

static RestData *
xml_spec_get_rest (GrlXmlFactorySource *source,
                   xmlNodePtr xml_node)
{
  RestData *rest_data;
  RestProxy *rest_proxy;
  gboolean needs_oauth;
  gchar *api_key;
  gchar *api_secret;
  gchar *api_token;
  gchar *api_token_secret;
  gchar *endpoint;
  xmlChar *xmlCharMethod;
  xmlChar *xmlCharReferer;
  gchar *method;
  gchar *function_name;
  gchar *param_name;
  gchar *param_value;
  xmlNodePtr xml_child_node;

  /* Check HTTP method (GET or POST, default GET) */
  xmlCharMethod = xmlGetProp (xml_node, (const xmlChar *) "method");
  if (STR_HAS_VALUE (xmlCharMethod)) {
    method = g_ascii_strup ((gchar *) xmlCharMethod, -1);
  } else {
    method = g_strdup ("GET");
  }
  xmlFree (xmlCharMethod);

  /* Check what is the endpoint, and check if we already have a RestProxy for it */
  needs_oauth = xml_get_property_boolean (xml_node, (const xmlChar *) "oauth");
  endpoint = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "endpoint");
  rest_proxy = find_rest_proxy (source->priv->rest_proxy_list, endpoint, needs_oauth);

  if (!rest_proxy) {
    if (needs_oauth) {
      /* Check we have everthing */
      api_key = grl_config_get_api_key (source->priv->config);
      api_secret = grl_config_get_api_secret (source->priv->config);
      if (!api_key || !api_secret) {
        GRL_DEBUG ("'api-key' or 'api-secret' not specified. Can not use RESTful operations");
        g_free (api_key);
        g_free (api_secret);
        g_free (endpoint);
        g_free (method);
        return NULL;
      }

      api_token = grl_config_get_api_token (source->priv->config);
      api_token_secret = grl_config_get_api_token_secret (source->priv->config);
      rest_proxy = oauth_proxy_new_with_token (api_key, api_secret,
                                               api_token, api_token_secret,
                                               endpoint, FALSE);
      g_free (api_key);
      g_free (api_secret);
      g_free (api_token);
      g_free (api_token_secret);
    } else {
      rest_proxy = rest_proxy_new (endpoint, FALSE);
    }
    if (source->priv->user_agent) {
      rest_proxy_set_user_agent (rest_proxy, source->priv->user_agent);
    }
    source->priv->rest_proxy_list = g_list_prepend (source->priv->rest_proxy_list,
                                                    rest_proxy);
  } else {
    rest_proxy = g_object_ref (rest_proxy);
  }

  rest_data = rest_data_new ();
  rest_data->proxy = rest_proxy;
  rest_data->endpoint = endpoint;
  rest_data->method = method;

  /* Check referer attribute */
  xmlCharReferer = xmlGetProp (xml_node, (const xmlChar *) "referer");
  if (STR_HAS_VALUE (xmlCharReferer)) {
    rest_data->referer = expandable_string_new ((const gchar *) xmlCharReferer,
                                                source->priv->config,
                                                source->priv->located_strings);
  }
  xmlFree (xmlCharReferer);

  xml_child_node = xml_get_node (xml_node->children);
  if (xmlStrcmp (xml_child_node->name, (const xmlChar *) "function") == 0) {
    function_name = (gchar *) xmlNodeGetContent (xml_child_node);
    rest_data->function = expandable_string_new (function_name,
                                                 source->priv->config,
                                                 source->priv->located_strings);
    g_free (function_name);
    xml_child_node = xml_get_node (xml_child_node->next);
  }

  /* Get the RESTful parameters */
  while (xml_child_node) {
    param_name = (gchar *) xmlGetProp (xml_child_node, (const xmlChar *) "name");
    param_value = (gchar *) xmlNodeGetContent (xml_child_node);
    rest_data->parameters =
      g_list_prepend (rest_data->parameters,
                      rest_parameter_new (param_name,
                                          expandable_string_new (param_value,
                                                                 source->priv->config,
                                                                 source->priv->located_strings)));
    g_free (param_value);
    xml_child_node = xml_get_node (xml_child_node->next);
  }

  return rest_data;
}

static RegExpData *
xml_spec_get_regexp (GrlXmlFactorySource *source,
                     xmlNodePtr xml_node)
{
  FetchData *subregexp;
  RegExpData *regexp;
  gchar *buffer_id;
  gchar *buffer_ref;

  xml_node = xml_get_node (xml_node->children);
  regexp = reg_exp_data_new ();

  /* Get the sub-regexpressions; we know underlying fetchdata contains a
     regexp */
  while (xmlStrcmp (xml_node->name, (const xmlChar *) "regexp") == 0) {
    subregexp = xml_spec_get_fetch_data (source, xml_node);
    if (!subregexp) {
      reg_exp_data_free (regexp);
      return NULL;
    }
    /* Tip: if the underlaying sub-regexp' output does not have an id, then it
       is totally irrelevant, as it can't be referenced by any other subregexp
       or this same regexp. So let's remove it to save time/memory */
    if (!subregexp->data.regexp->output_id) {
      fetch_data_free (subregexp);
    } else {
      regexp->subregexp = g_list_append (regexp->subregexp, subregexp);
    }
    xml_node = xml_get_node (xml_node->next);
  }

  /* Get the input */
  regexp->input->decode = xml_get_property_boolean (xml_node, (const xmlChar *) "decode");
  buffer_ref = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "ref");
  if (STR_HAS_VALUE (buffer_ref)) {
    regexp->input->use_ref = TRUE;
    regexp->input->data.buffer_id = buffer_ref;
  } else {
    g_free (buffer_ref);
    regexp->input->use_ref = FALSE;
    regexp->input->data.input =
      xml_spec_get_fetch_data (source, xml_get_node (xml_node->children));
  }

  /* Get the output */
  xml_node = xml_get_node (xml_node->next);
  if (xml_node &&
      xmlStrcmp (xml_node->name, (const xmlChar *) "output") == 0) {
    regexp->output = xml_spec_get_expandable_string (source, xml_node);
    buffer_id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "id");
    if (STR_HAS_VALUE (buffer_id)) {
      regexp->output_id = buffer_id;
    } else {
      g_free (buffer_id);
    }
    xml_node = xml_get_node (xml_node->next);
  }

  /* Get the expression */
  if (xml_node) {
    regexp->expression->expression = xml_spec_get_expandable_string (source, xml_node);
    regexp->expression->repeat = xml_get_property_boolean (xml_node, (const xmlChar *) "repeat");
  }

  return regexp;
}

static ReplaceData *
xml_spec_get_replace (GrlXmlFactorySource *source,
                      xmlNodePtr xml_node)
{
  FetchData *input_data;
  ReplaceData *replace_data;

  xml_node = xml_get_node (xml_node->children);
  replace_data = replace_data_new ();

  /* Get the input */
  input_data = xml_spec_get_fetch_data (source, xml_get_node (xml_node->children));
  if (!input_data) {
    replace_data_free (replace_data);
    return NULL;
  }
  replace_data->input = input_data;

  /* Get the replacement, if available */
  xml_node = xml_get_node (xml_node->next);
  if (xmlStrcmp (xml_node->name, (const xmlChar *) "replacement") == 0) {
    replace_data->replacement = xml_spec_get_expandable_string (source, xml_node);
    xml_node = xml_get_node (xml_node->next);
  } else {
    replace_data->replacement = NULL;
  }

  /* Get the expression */
  replace_data->expression = xml_spec_get_expandable_string (source, xml_node);

  return replace_data;
}

static FetchData *
xml_spec_get_fetch_data (GrlXmlFactorySource *source,
                         xmlNodePtr xml_node)
{
  ExpandableString *raw = NULL;
  ExpandableString *script_data = NULL;
  FetchData *data = NULL;
  FetchData *url_data = NULL;
  RegExpData *regexp_data = NULL;
  ReplaceData *replace_data = NULL;
  RestData *rest_data = NULL;
  gchar *dump_file;

  /* Check if there is result */
  if (!xml_node) {
    return NULL;
  }

  if (xml_node->type == XML_TEXT_NODE ||
      xml_node->type == XML_CDATA_SECTION_NODE) {
    raw = xml_spec_get_expandable_string (source, xml_node);
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "script") == 0) {
    script_data =
      xml_spec_get_expandable_string (source, xml_node);
    if (!script_data) {
      return NULL;
    }
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "url") == 0) {
    url_data =
      xml_spec_get_fetch_data (source, xml_get_node (xml_node->children));
    if (!url_data) {
      return NULL;
    }
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "rest") == 0) {
    rest_data = xml_spec_get_rest (source, xml_node);
    if (!rest_data) {
      return NULL;
    }
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "regexp")  == 0) {
    regexp_data = xml_spec_get_regexp (source, xml_node);
    if (!regexp_data) {
      return NULL;
    }
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "replace") == 0) {
    replace_data = xml_spec_get_replace (source, xml_node);
    if (!replace_data) {
      return NULL;
    }
  }

  if (raw) {
    data = fetch_data_new ();
    data->type = FETCH_RAW;
    data->data.raw = raw;
  } else if (script_data) {
    data = fetch_data_new ();
    data->type = FETCH_SCRIPT;
    data->data.raw = script_data;
  } else if (url_data) {
    data = fetch_data_new ();
    data->type = FETCH_URL;
    data->data.url = url_data;
  } else if (rest_data) {
    data = fetch_data_new ();
    data->type = FETCH_REST;
    data->data.rest = rest_data;
  } else if (replace_data) {
    data = fetch_data_new ();
    data->type = FETCH_REPLACE;
    data->data.replace = replace_data;
  } else if (regexp_data) {
    data = fetch_data_new ();
    data->type = FETCH_REGEXP;
    data->data.regexp = regexp_data;
  }

  /* Get debug dump */
  if (!raw && data) {
    dump_file = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "dump");
    if (STR_HAS_VALUE (dump_file)) {
      data->dump = log_dump_data_new (source,
                                      (guint) xmlGetLineNo (xml_node),
                                      dump_file);
    }
    g_free (dump_file);
  }

  return data;
}

/* Returns the GType of the media specified in the "type" property in
   @xml_node */
static GType
xml_spec_get_media_type (xmlNodePtr xml_node)
{
  xmlChar *type;
  GType media_type;

  type = xmlGetProp (xml_node, (const xmlChar *) "type");

  /* Get element type */
  if (xmlStrcmp (type, (const xmlChar *) "audio") == 0) {
    media_type = GRL_TYPE_MEDIA_AUDIO;
  } else if (xmlStrcmp (type, (const xmlChar *) "video") == 0) {
    media_type = GRL_TYPE_MEDIA_VIDEO;
  } else if (xmlStrcmp (type, (const xmlChar *) "image") == 0) {
    media_type = GRL_TYPE_MEDIA_IMAGE;
  } else if (xmlStrcmp (type, (const xmlChar *) "box") == 0) {
    media_type = GRL_TYPE_MEDIA_BOX;
  } else {
    media_type = GRL_TYPE_MEDIA;
  }

  xmlFree (type);

  return media_type;
}

/* Get the basic information from XML spec */
static void
xml_spec_get_basic_info (xmlNodePtr xml_node,
                         gchar **id,
                         gchar **name,
                         gchar **description,
                         gchar **icon,
                         xmlNodePtr *strings,
                         xmlNodePtr *config,
                         xmlNodePtr *script,
                         xmlNodePtr *operation,
                         xmlNodePtr *provide)
{
  *id = (gchar *) xmlNodeGetContent (xml_node);
  xml_node = xml_get_node (xml_node->next);

  *name = (gchar *) xmlNodeGetContent (xml_node);
  xml_node = xml_get_node (xml_node->next);

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "description") == 0) {
    *description = (gchar *) xmlNodeGetContent (xml_node);
    xml_node = xml_get_node (xml_node->next);
  } else {
    *description = NULL;
  }

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "icon") == 0) {
    *icon = (gchar *) xmlNodeGetContent (xml_node);
    xml_node = xml_get_node (xml_node->next);
  } else {
    *icon = NULL;
  }

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "strings") == 0) {
    *strings = xml_node;
    do {
      xml_node = xml_get_node (xml_node->next);
    } while (xmlStrcmp (xml_node->name, (const xmlChar *) "strings") == 0);
  } else {
    *strings = NULL;
  }

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "config") == 0) {
    *config = xml_get_node (xml_node->children);
    xml_node = xml_get_node (xml_node->next);
  } else {
    *config = NULL;
  }

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "script") == 0) {
    *script = xml_get_node (xml_node->children);
    xml_node = xml_get_node (xml_node->next);
  } else {
    *script = NULL;
  }

  *operation = xml_node;
  xml_node = xml_get_node (xml_node->next);

  *provide = xml_node;
}

/* Creates a Grilo config with the default values defined in XML spec; it also
   adds in %config_keys the list of required config keys */
static GrlConfig *
xml_spec_get_config (xmlNodePtr xml_config_node, const gchar *source_id, GList **config_keys)
{
  GrlConfig *config;
  gchar *conf_name;
  gchar *conf_value;

  config = grl_config_new (XML_FACTORY_PLUGIN_ID, source_id);
  while (xml_config_node) {
    conf_name = (gchar *) xmlGetProp (xml_config_node, (const xmlChar *) "name");
    *config_keys = g_list_prepend (*config_keys, conf_name);
    conf_value = (gchar *) xmlNodeGetContent (xml_config_node);
    if (STR_HAS_VALUE (conf_value)) {
      grl_config_set_string (config, conf_name, conf_value);
    }
    g_free (conf_value);
    xml_config_node = xml_get_node (xml_config_node->next);
  }

  return config;
}

/* Creates a hashtable of (str_id, str_value) for the located strings */
static GHashTable *
xml_spec_get_located_strings (xmlNodePtr xml_node)
{
  GHashTable *strings;
  gchar *id;
  gchar *value;

  strings = g_hash_table_new_full (g_str_hash,
                                   g_str_equal,
                                   g_free,
                                   g_free);

  while (xml_node) {
    id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "id");
    value = (gchar *) xmlNodeGetContent (xml_node);
    if (STR_HAS_VALUE (id) && STR_HAS_VALUE (value)) {
      g_hash_table_insert (strings, id, value);
    } else {
      g_free (id);
      g_free (value);
    }
    xml_node = xml_get_node (xml_node->next);
  }

  return strings;
}

/* Stores the pre-defined strings, using only those that makes sense for the
   current language */
static GList *
xml_spec_get_strings (xmlNodePtr xml_node)
{
  GHashTable **defined_strings_set;
  GList *located_strings = NULL;
  const gchar * const *current_languages;
  gchar *language;
  gint i;

  current_languages = g_get_language_names ();
  defined_strings_set = g_new0 (GHashTable *, g_strv_length ((gchar **) current_languages));

  while (xmlStrcmp (xml_node->name, (const xmlChar *) "strings") == 0) {
    /* Check the language */
    language = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "lang");
    if (!STR_HAS_VALUE (language)) {
      g_free (language);
      language = NULL;
    }

    /* Check if we are using that language right now */
    i = 0;
    while (current_languages[i]) {
      if (!language &&
          strcmp (current_languages[i], "C") == 0) {
        break;
      }

      if (language &&
          g_ascii_strcasecmp (current_languages[i], language) == 0) {
        break;
      }

      i++;
    }

    if (current_languages[i]) {
      /* Special case: if user defines twice a set of strings for the same
         language, use the last one */
      if (defined_strings_set[i]) {
        g_hash_table_unref (defined_strings_set[i]);
      }
      defined_strings_set[i] = xml_spec_get_located_strings (xml_get_node (xml_node->children));
    }

    g_free (language);
    xml_node = xml_get_node (xml_node->next);
  }

  /* Now, add the defined and supported languages in a list, order by the
     priority of the language (the more specific first) */
  for (i = 0; i < g_strv_length ((gchar **) current_languages); i++) {
    if (defined_strings_set[i]) {
      located_strings = g_list_append (located_strings,
                                       defined_strings_set[i]);
    }
  }

  g_free (defined_strings_set);

  return located_strings;
}

static lua_State *
xml_spec_get_init_script (xmlNodePtr xml_node,
                          GrlConfig *config,
                          GList *strings)
{
  lua_State *L;
  ExpandableString *lua_script;
  gchar *lua_script_str;
  int lua_status;

  L = luaL_newstate ();
  if (!L) {
    goto error;
  }

  luaL_openlibs (L);

  lua_script = xml_spec_get_expandable_string_impl (xml_node, config, strings);
  lua_script_str = expandable_string_get_value (lua_script, NULL);

  /* Load the initialization script and check if it succeded */
  lua_status = luaL_loadstring(L, lua_script_str);
  expandable_string_free_value (lua_script, lua_script_str);
  expandable_string_free (lua_script);

  if (lua_status) {
    goto error;
  }
  lua_status = lua_pcall (L, 0, 1, 0);
  if (lua_status) {
    goto error;
  }
  if (lua_type (L, -1) != LUA_TNIL &&
      !lua_toboolean (L, -1)) {
    goto error;
  }
  lua_pop (L, 1);
  return L;

 error:
  if (L) {
    lua_close (L);
  }
  return NULL;
}

static gint
xml_spec_get_format (xmlNodePtr xml_node)
{
  gint format;
  xmlChar *str_format;

  str_format = xmlGetProp (xml_node, (const xmlChar *) "format");
  if (str_format &&
      xmlStrcmp (str_format, (const xmlChar *) "json") == 0) {
    format = FORMAT_JSON;
  } else {
    format = FORMAT_XML;
  }
  xmlFree (str_format);

  return format;
}

static MediaTemplate *
xml_spec_get_provide_media_template (GrlXmlFactorySource *source,
                                     xmlNodePtr xml_node,
                                     GHashTable *required_keys)
{
  FetchData *data;
  GrlKeyID grl_key;
  GrlRegistry *registry;
  MediaTemplate *template;
  PrivateData *prdata;
  gboolean forced;
  gboolean slow;
  gchar *key_name;
  gchar *query;
  gchar *raw;
  gchar *select;
  gchar *source_id;
  int i;
  xmlChar *use_value;
  xmlNodePtr xml_key;
  xmlNs *ns;

  template = media_template_new ();
  template->media_type = xml_spec_get_media_type (xml_node);

  template->line_number = xmlGetLineNo (xml_node);

  if (xml_node->nsDef) {
    /* Save the NS, as we will free this XML doc */
    for (ns = xml_node->nsDef, i = 0; ns; ns = ns->next, i++);
    template->namespace = g_new0 (NameSpace, i);
    for (ns = xml_node->nsDef; ns; ns = ns->next) {
      if (STR_HAS_VALUE (ns->prefix)) {
        template->namespace[template->namespace_size].prefix =
          xmlCharStrdup ((const char *) ns->prefix);
        template->namespace[template->namespace_size].href =
          xmlCharStrdup ((const char *) ns->href);
        template->namespace_size++;
      }
    }
  }

  template->format = xml_spec_get_format (xml_node);

  template->operation_id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "ref");

  query = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "query");
  if (STR_HAS_VALUE (query)) {
      template->query = expandable_string_new (query,
                                               source->priv->config,
                                               source->priv->located_strings);
  }
  g_free (query);

  select = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "select");
  if (STR_HAS_VALUE (select)) {
    template->select = expandable_string_new (select,
                                              source->priv->config,
                                              source->priv->located_strings);
  }
  g_free (select);

  registry = grl_registry_get_default ();

  /* Add the keys and the method to get the values */
  for (xml_key = xml_get_node (xml_node->children);
       xml_key;
       xml_key = xml_get_node (xml_key->next)) {
    key_name = (gchar *) xmlGetProp (xml_key, (const xmlChar *) "name");
    if (xmlStrcmp (xml_key->name, (const xmlChar *) "priv") == 0) {
      raw = (gchar *) xmlNodeGetContent (xml_key);
      if (!STR_HAS_VALUE (raw)) {
        g_free (raw);
        g_free (key_name);
        continue;
      }

      /* Prefix source_id */
      prdata = private_data_new ();
      g_object_get (G_OBJECT (source), "source-id", &source_id, NULL);
      prdata->name = g_strconcat (source_id, "::", key_name, NULL);
      g_free (source_id);
      g_free (key_name);
      prdata->data = expandable_string_new (raw,
                                            source->priv->config,
                                            source->priv->located_strings);
      template->private_keys = g_list_prepend (template->private_keys, prdata);
      g_free (raw);
      continue;
    }
    grl_key = grl_registry_lookup_metadata_key (registry, key_name);
    if (grl_key == GRL_METADATA_KEY_INVALID) {
      GRL_WARNING ("Invalid key '%s': not registered; ignoring", key_name);
      g_free (key_name);
      continue;
    }

    if (!xml_spec_key_is_supported (grl_key)) {
      GRL_WARNING ("Invalid key '%s': unsupported type; ignoring", key_name);
      g_free (key_name);
      continue;
    }

    g_free (key_name);

    if (xml_key->children) {
      data = xml_spec_get_fetch_data (source,
                                      xml_get_node (xml_key->children));
    } else {
      data = NULL;
    }

    if (data) {
      g_hash_table_insert (template->keys,
                           GRLKEYID_TO_POINTER (grl_key),
                           data);
    }

    /* Check if this key is compulsory */
    forced = xml_get_property_boolean (xml_key, (const xmlChar *) "force");
    if (forced) {
      template->mandatory_keys = g_list_prepend (template->mandatory_keys,
                                                 GRLKEYID_TO_POINTER (grl_key));
    }

    /* Check if it is a slow key */
    slow = xml_get_property_boolean (xml_key, (const xmlChar *) "slow");
    if (slow &&
        g_list_find (source->priv->slow_keys,
                     GRLKEYID_TO_POINTER (grl_key)) == NULL) {
      source->priv->slow_keys =
        g_list_prepend (source->priv->slow_keys,
                        GRLKEYID_TO_POINTER (grl_key));
    }

    /* If key is not forced but it is a non-slow key and is involved in some
       operation requirement, then include it too as compulsory */
    if (!forced && !slow &&
        g_hash_table_lookup_extended (required_keys, GRLKEYID_TO_POINTER (grl_key), NULL, NULL)) {
      template->mandatory_keys = g_list_prepend (template->mandatory_keys,
                                                 GRLKEYID_TO_POINTER (grl_key));
    }

    /* Check if it must be resolved through resolve() operation */
    use_value = xmlGetProp (xml_key, (const xmlChar *) "use");
    if (xmlStrcmp (use_value, (const xmlChar *) "resolve") == 0) {
      source->priv->use_resolve_keys =
        g_list_prepend (source->priv->use_resolve_keys,
                        GRLKEYID_TO_POINTER (grl_key));
    }
    xmlFree (use_value);

    /* Add they key to the list of supported keys by source */
    if (g_list_find (source->priv->supported_keys,
                     GRLKEYID_TO_POINTER (grl_key)) == NULL) {
      source->priv->supported_keys =
        g_list_prepend (source->priv->supported_keys,
                        GRLKEYID_TO_POINTER (grl_key));
    }
  }

  return template;
}

static void
xml_spec_get_operation_parameters (GrlXmlFactorySource *source,
                                   xmlNodePtr xml_node,
                                   gchar **id,
                                   ExpandableString **skip,
                                   ExpandableString **count)
{
  gchar *xml_count;
  gchar *xml_skip;

  xml_skip = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "skip");
  xml_count = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "count");
  *id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "id");

  *skip = expandable_string_new (xml_skip,
                                 source->priv->config,
                                 source->priv->located_strings);
  *count = expandable_string_new (xml_count,
                                  source->priv->config,
                                  source->priv->located_strings);

  g_free (xml_skip);
  g_free (xml_count);
}

static void
xml_spec_get_resolve_properties (xmlNodePtr xml_node,
                                 Operation *operation)
{
  GrlKeyID key_id;
  GrlRegistry *registry;
  gchar *key_name;

  key_name = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "key");
  if (key_name) {
    registry = grl_registry_get_default ();
    key_id = grl_registry_lookup_metadata_key (registry, key_name);
    if (xml_spec_key_is_supported (key_id)) {
      operation->resolve_key = key_id;
    } else {
      GRL_WARNING ("Invalid key '%s': unsupported type; ignoring", key_name);
    }
  }

  g_free (key_name);

  operation->resolve_any = xml_get_property_boolean (xml_node, (const xmlChar *) "any");
}

static void
xml_spec_get_operation_requirements (xmlNodePtr *node,
                                     Operation *operation)
{
  OperationRequirement *req;
  GError *error = NULL;
  GRegex *match;
  GrlKeyID grl_key;
  GrlRegistry *registry;
  gchar *key_name;
  xmlChar *match_exp;
  xmlNodePtr node_child;

  if (xmlStrcmp ((*node)->name, (const xmlChar *) "require") != 0) {
    /* No requirement */
    return;
  }

  operation->required_type = xml_spec_get_media_type (*node);

  registry = grl_registry_get_default ();
  for (node_child = xml_get_node ((*node)->children);
       node_child;
       node_child = xml_get_node (node_child->next)) {
    key_name = (gchar *) xmlGetProp (node_child, (const xmlChar *) "name");
    grl_key = grl_registry_lookup_metadata_key (registry, key_name);

    if (grl_key == GRL_METADATA_KEY_INVALID) {
      GRL_WARNING ("Invalid key '%s': not registered; ignoring", key_name);
      g_free (key_name);
      continue;
    }

    if (grl_metadata_key_get_type (grl_key) != G_TYPE_STRING) {
      GRL_WARNING ("Invalid key '%s': unsupported type; ignoring", key_name);
      g_free (key_name);
      continue;
    }
    g_free (key_name);

    match_exp = xmlNodeGetContent (node_child);
    if (STR_HAS_VALUE (match_exp)) {
      match = g_regex_new ((const gchar *) match_exp, G_REGEX_OPTIMIZE, 0, &error);
      if (error) {
        GRL_WARNING ("Wrong match expression '%s': '%s; ignoring",
                     match_exp, error->message);
        g_error_free (error);
        xmlFree (match_exp);
        continue;
      }
    } else {
      match = NULL;
    }

    xmlFree (match_exp);

    req = operation_requirement_new ();
    req->key = grl_key;
    req->match_reg = match;
    operation->requirements = g_list_prepend (operation->requirements, req);
  }

  *node = xml_get_node ((*node)->next);
}

static ResultData *
xml_spec_get_operation_result (GrlXmlFactorySource *source,
                               xmlNodePtr xml_node)
{
  ResultData *result_data;
  gchar *result_id;
  xmlChar *cache_time_str;

  result_id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "ref");
  if (result_id) {
    /* Let's reuse a previous ResultData */
    if (source->priv->results) {
      result_data = g_hash_table_lookup (source->priv->results, result_id);
    } else {
      result_data = NULL;
    }
    if (result_data) {
      return result_data_ref (result_data);
    } else {
      return NULL;
    }
  }

  result_data = result_data_new ();
  result_data->format = xml_spec_get_format (xml_node);
  cache_time_str = xmlGetProp (xml_node, (const xmlChar *) "cache");
  if (STR_HAS_VALUE (cache_time_str)) {
    result_data->cache_time = (guint) g_ascii_strtoull ((const gchar *) cache_time_str, NULL, 10);
  }
  xmlFree (cache_time_str);

  result_data->query = xml_spec_get_fetch_data (source, xml_get_node (xml_node->children));
  if (!result_data->query) {
    result_data_unref (result_data);
    return NULL;
  }
  /* Check if result must be saved for further use */
  result_id = (gchar *) xmlGetProp (xml_node, (const xmlChar *) "id");
  if (result_id) {
    if (!source->priv->results) {
      source->priv->results = g_hash_table_new_full (g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     (GDestroyNotify) result_data_unref);
    }
    g_hash_table_insert (source->priv->results, result_id, result_data_ref (result_data));
  }

  return result_data;
}

static Operation *
xml_spec_get_operation_data (GrlXmlFactorySource *source,
                             xmlNodePtr xml_node)
{
  Operation *operation;

  operation = operation_new ();

  operation->line_number = xmlGetLineNo (xml_node);

  xml_spec_get_operation_parameters (source, xml_node,
                                     &(operation->id),
                                     &(operation->skip),
                                     &(operation->count));

  xml_spec_get_resolve_properties (xml_node, operation);

  xml_node = xml_get_node (xml_node->children);

  xml_spec_get_operation_requirements (&xml_node, operation);
  operation->result = xml_spec_get_operation_result (source, xml_node);

  if (!operation->result) {
    operation_free (operation);
    return NULL;
  }

  return operation;
}

static void
browse_send_result_cb (GrlMedia *media,
                       gint remaining,
                       GrlSourceBrowseSpec *bs,
                       GError *error)
{
  if (error && !error->code) {
    error->code = GRL_CORE_ERROR_BROWSE_FAILED;
  }

  bs->callback (bs->source,
                bs->operation_id,
                media,
                remaining,
                bs->user_data,
                error);
}

static void
search_send_result_cb (GrlMedia *media,
                       gint remaining,
                       GrlSourceSearchSpec *ss,
                       GError *error)
{
  if (error && !error->code) {
    error->code = GRL_CORE_ERROR_SEARCH_FAILED;
  }

  ss->callback (ss->source,
                ss->operation_id,
                media,
                remaining,
                ss->user_data,
                error);
}

static void
resolve_send_result_cb (GrlMedia *media,
                        gint remaining,
                        GrlSourceResolveSpec *rs,
                        GError *error)
{
  GHashTable *new_private_keys;
  GHashTable *old_private_keys;
  GList *k;
  GList *keys;
  gchar *new_private_keys_string;

  if (error && !error->code) {
    error->code = GRL_CORE_ERROR_RESOLVE_FAILED;
  }

  /* We need to update the media sent by user; so let's merge both medias */
  if (media) {
    /* Merge private keys */

    new_private_keys =
      json_ghashtable_deserialize_data (grl_data_get_string (GRL_DATA (media),
                                                             GRL_METADATA_KEY_PRIVATE_KEYS),
                                        -1,
                                        NULL);
    old_private_keys =
      json_ghashtable_deserialize_data (grl_data_get_string (GRL_DATA (rs->media),
                                                             GRL_METADATA_KEY_PRIVATE_KEYS),
                                        -1,
                                        NULL);
    if (new_private_keys) {
      if (old_private_keys) {
        merge_hashtables (new_private_keys, old_private_keys);
        new_private_keys_string = json_ghashtable_serialize_data (new_private_keys, NULL);
        g_hash_table_unref (old_private_keys);
      } else {
        new_private_keys_string = NULL;
      }
      g_hash_table_unref (new_private_keys);
      if (new_private_keys_string) {
        grl_data_set_string (GRL_DATA (media),
                             GRL_METADATA_KEY_PRIVATE_KEYS,
                             new_private_keys_string);
        g_free (new_private_keys_string);
      }
    } else {
      if (old_private_keys) {
        new_private_keys_string = json_ghashtable_serialize_data (old_private_keys, NULL);
        grl_data_set_string (GRL_DATA (media),
                             GRL_METADATA_KEY_PRIVATE_KEYS,
                             new_private_keys_string);
        g_free (new_private_keys_string);
        g_hash_table_unref (old_private_keys);
      }
    }

    /* Merge remaining keys */
    keys = grl_data_get_keys (GRL_DATA (media));

    for (k = keys; k; k = g_list_next (k)) {
      grl_data_set (GRL_DATA (rs->media),
                    GRLPOINTER_TO_KEYID (k->data),
                    grl_data_get (GRL_DATA (media),
                                  GRLPOINTER_TO_KEYID (k->data)));
    }
    g_list_free (keys);
    g_object_unref (media);
  }

  rs->callback (rs->source,
                rs->operation_id,
                rs->media,
                rs->user_data,
                error);
}

static void
fetch_data_obtained (const gchar *content,
                     FetchItemData *data,
                     const GError *error)
{
  if (!error && content) {
    insert_value (data->op_data->source, data->item->media, data->key, content);
  }

  data->item->pending_count--;
  operation_call_send_list_run (data->op_data);
  fetch_item_data_free (data);
}


static void
xml_spec_get_operation (GrlXmlFactorySource *source,
                        xmlNodePtr xml_node)
{
  Operation *operation = NULL;

  operation = xml_spec_get_operation_data (source, xml_node);

  if (!operation) {
    return;
  }

  if (xmlStrcmp (xml_node->name, (const xmlChar *) "search") == 0) {
    source->priv->operations[OP_SEARCH] =
      g_list_append ((GList *) source->priv->operations[OP_SEARCH],
                     operation);
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "browse") == 0) {
    source->priv->operations[OP_BROWSE] =
      g_list_append ((GList *) source->priv->operations[OP_BROWSE],
                     operation);
  } else if (xmlStrcmp (xml_node->name, (const xmlChar *) "resolve") == 0) {
    source->priv->operations[OP_RESOLVE] =
      g_list_append ((GList *) source->priv->operations[OP_RESOLVE],
                     operation);
  }
}

static guint
expandable_string_to_number (ExpandableString *exp_str,
                             ExpandData *expand_data,
                             guint default_value)
{
  gchar *str;
  guint str_number;

  str = expandable_string_get_value (exp_str, expand_data);
  if (str) {
    str_number = (guint) g_ascii_strtoull (str, NULL, 10);
    expandable_string_free_value (exp_str, str);
    if (str_number != 0) {
      return str_number;
    } else {
      return default_value;
    }
  }

  return default_value;
}

static gchar *
get_raw_from_path (GrlXmlFactorySource *source,
                   ExpandableString *raw,
                   DataRef *data)
{
  GError *error = NULL;
  GValue value = { 0 };
  GetRawData *raw_data;
  JsonArray *json_array;
  JsonNode *json_first_node;
  JsonNode *json_node;
  gchar *expanded_raw;
  gchar *json_value = NULL;
  gchar *xpath_strvalue;
  int i;
  xmlDocPtr xml_doc;
  xmlXPathContextPtr xml_ctx;
  xmlXPathObjectPtr xpath;
  xmlXPathObjectPtr xpath_value;

  raw_data = dataref_value (data);
  expanded_raw = expandable_string_get_value (raw, raw_data->expand_data);

  if (!expanded_raw) {
    return NULL;
  }

  if (raw_data->xml_doc_reffed) {
    xml_doc = dataref_value (raw_data->xml_doc_reffed);
    xml_ctx = dataref_value (raw_data->xml_ctx_reffed);
    xpath = dataref_value (raw_data->xpath_reffed);
    xml_ctx->node = xpath->nodesetval->nodeTab[raw_data->node];
    xmlXPathRegisteredNsCleanup (xml_ctx);
    if (raw_data->namespace) {
      for (i = 0; i < raw_data->namespace_size; i++) {
        xmlXPathRegisterNs(xml_ctx, raw_data->namespace[i].prefix, raw_data->namespace[i].href);
      }
    }

    xpath_value = xmlXPathEvalExpression ((const xmlChar *) expanded_raw, xml_ctx);
    if (!xpath_value) {
      GRL_DEBUG ("XPath '%s' did not return any result", expanded_raw);
      expandable_string_free_value (raw, expanded_raw);
      return NULL;
    }

    expandable_string_free_value (raw, expanded_raw);
    if (xpath_value->type == XPATH_NODESET &&
        xpath_value->nodesetval &&
        xpath_value->nodesetval->nodeTab) {
      xpath_strvalue = (gchar *) xmlNodeListGetString (xml_doc,
                                                       xpath_value->nodesetval->nodeTab[0]->xmlChildrenNode, 1);
      xmlXPathFreeObject (xpath_value);
      return xpath_strvalue;
    }

    if (xpath_value->type == XPATH_STRING &&
        STR_HAS_VALUE (xpath_value->stringval)) {
      xpath_strvalue = g_strdup ((const gchar *) xpath_value->stringval);
      xmlXPathFreeObject (xpath_value);
      return xpath_strvalue;
    }
    xmlXPathFreeObject (xpath_value);
  }


  if (raw_data->json_array) {
    json_node = json_path_query (expanded_raw,
                                 json_array_get_element (raw_data->json_array,
                                                         raw_data->node),
                                 &error);

    if (!json_node) {
      if (error) {
        GRL_DEBUG ("JSONPath '%s' error: %s", expanded_raw, error->message);
        g_error_free (error);
      } else {
        GRL_DEBUG ("JSONPath '%s' error", expanded_raw);
      }
      expandable_string_free_value (raw, expanded_raw);

      return NULL;
    }

    json_array = json_node_get_array (json_node);

    if (json_array_get_length (json_array) == 0) {
      GRL_DEBUG ("JSONPath '%s' did not return any result", expanded_raw);
      expandable_string_free_value (raw, expanded_raw);
      json_node_free (json_node);
      return NULL;
    }

    expandable_string_free_value (raw, expanded_raw);
    json_first_node = json_array_get_element (json_array, 0);
    if (JSON_NODE_HOLDS_VALUE (json_first_node)) {
      json_node_get_value (json_first_node, &value);
      if (G_VALUE_HOLDS_STRING (&value)) {
        json_value = g_value_dup_string (&value);
      } else if (G_VALUE_HOLDS_INT64 (&value)) {
        json_value = g_strdup_printf ("%li", (long int) g_value_get_int64 (&value));
      } else if (G_VALUE_HOLDS_DOUBLE (&value)) {
        json_value = g_strdup_printf ("%f", g_value_get_double (&value));
      }
      g_value_unset (&value);
    }
    json_node_free (json_node);

    return json_value;
  }

  return NULL;
}

static gchar *
get_raw_from_operation (GrlXmlFactorySource *source,
                        ExpandableString *raw,
                        DataRef *data)
{
  ExpandData *expand_data;

  expand_data = dataref_value (data);

  return expandable_string_get_value (raw, expand_data);
}

static void
use_resolve_done_cb (GrlMedia *media,
                     gint remaining,
                     OperationCallData *data,
                     GError *error)
{
  SendItem *send_item = (SendItem *) data->send_list->data;
  send_item->pending_count--;

  operation_call_send_list_run (data);
}

static gboolean
operation_call_was_cancelled (OperationCallData *data)
{
  GError *error;

  if (g_cancellable_is_cancelled (data->cancellable)) {
    error = g_error_new (GRL_CORE_ERROR,
                         GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation has been cancelled");
    data->callback (NULL, 0, data->user_data, error);
    g_error_free (error);
    operation_call_data_free (data);
    return TRUE;
  } else {
    return FALSE;
  }
}

static void
operation_call_send_list_run (OperationCallData *data)
{
  OperationCallData *resolve_data;
  SendItem *send_item;

  /* Start to send all elements when there are no pending operations over each
     element */
  while (data->send_list) {
    send_item = (SendItem *) data->send_list->data;
    if (send_item->pending_count == 0) {
      /* Check if elements must go through resolve() before sending */
      if (send_item->apply_resolve) {
        send_item->apply_resolve = FALSE;
        send_item->pending_count++;
        resolve_data = get_resolve_data (data->source,
                                         data->cancellable,
                                         send_item->media,
                                         data->keys,
                                         data->options,
                                         (SendResultCb) use_resolve_done_cb,
                                         data);
        if (resolve_data) {
          operation_call (resolve_data);
          return;
        }
      }
      data->callback (send_item->media,
                      --(data->total_results),
                      data->user_data,
                      NULL);
      send_item_free (send_item);
      data->send_list = g_list_delete_link (data->send_list, data->send_list);
    } else {
      return;
    }
  }

  if (data->total_results == 0) {
    operation_call_data_free (data);
  }
}

static gboolean
operation_call_send_xml_results (OperationCallData *data)
{
  DataRef *get_raw_data_reffed;
  DataRef *media_template_xpath_reffed;
  DataRef *xml_ctx_reffed;
  DataRef *xml_doc_reffed;
  ExpandableString *xpath_query;
  FetchData *fetch_data;
  FetchItemData *fetch_item;
  GHashTable *private_keys;
  GList *k;
  GList *keys;
  GList *matching_templates = NULL;
  GList *matching_xpath = NULL;
  GList *prdata_list;
  GList *pt;
  GList *px;
  GetRawData *get_raw_data;
  MediaTemplate *media_template;
  PrivateData *prdata;
  SendItem *send_item;
  gchar *json_data;
  gchar *prvalue;
  gchar *xpath;
  gint pending;
  guint skip;
  int i;
  xmlDocPtr xml_doc;
  xmlXPathContextPtr xml_ctx;
  xmlXPathObjectPtr media_template_xpath;

  if (operation_call_was_cancelled (data)) {
    return FALSE;
  }

  xml_doc_reffed = dataref_ref (data->xml_doc_reffed);
  xml_doc = dataref_value (xml_doc_reffed);
  xml_ctx = xmlXPathNewContext (xml_doc);
  xml_ctx_reffed = dataref_new (xml_ctx, (GDestroyNotify) xmlXPathFreeContext);


  /* Scan all media templates, finding those that match the results */
  GRL_XML_DEBUG_LITERAL (data->source,
                         GRL_XML_DEBUG_PROVIDE,
                         "Selecting XML provide template");
  for (pt = data->source->priv->media_templates;
       pt && data->total_results < (data->skip + data->count);
       pt = g_list_next (pt)) {
    media_template = (MediaTemplate *) pt->data;

    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Testing template in line %ld",
                   media_template->line_number);

    if (media_template->format != FORMAT_XML) {
      GRL_XML_DEBUG_LITERAL (data->source,
                             GRL_XML_DEBUG_PROVIDE,
                             "Failed: not a XML template");
      continue;
    }

    if (media_template->operation_id &&
        g_strcmp0 (media_template->operation_id, data->operation->id) != 0) {
      GRL_XML_DEBUG (data->source,
                     GRL_XML_DEBUG_PROVIDE,
                     "Failed: expected operation id '%s', current is '%s'",
                     media_template->operation_id,
                     data->operation->id);
      continue;
    }

    xmlXPathRegisteredNsCleanup (xml_ctx);

    /* Register the namespaces */
    if (media_template->namespace) {
      for (i = 0; i < media_template->namespace_size; i++) {
        xmlXPathRegisterNs(xml_ctx, media_template->namespace[i].prefix, media_template->namespace[i].href);
      }
    }

    if (data->operation_type == OP_RESOLVE) {
      if (media_template->select) {
        xpath = expandable_string_get_value (media_template->select, data->expand_data);
        media_template_xpath = xmlXPathEvalExpression ((const xmlChar *) xpath, xml_ctx);
        if (!media_template_xpath) {
          GRL_XML_DEBUG (data->source,
                         GRL_XML_DEBUG_PROVIDE,
                         "Failed: XPath '%s' is invalid",
                         xpath);
          expandable_string_free_value (media_template->select, xpath);
          continue;
        }
        xpath_query = media_template->select;

      } else {
      GRL_XML_DEBUG_LITERAL (data->source,
                             GRL_XML_DEBUG_PROVIDE,
                             "Failed: template does not provide XPath 'select'");
      continue;
      }
    } else {
      if (media_template->query) {
        xpath = expandable_string_get_value (media_template->query, data->expand_data);
        media_template_xpath = xmlXPathEvalExpression ((const xmlChar *) xpath, xml_ctx);
        if (!media_template_xpath) {
          GRL_XML_DEBUG (data->source,
                         GRL_XML_DEBUG_PROVIDE,
                         "Failed: XPath '%s' is invalid",
                         xpath);
          expandable_string_free_value (media_template->select, xpath);
          continue;
        }
        xpath_query = media_template->query;
      } else {
        GRL_XML_DEBUG_LITERAL (data->source,
                               GRL_XML_DEBUG_PROVIDE,
                               "Failed: template does not provide XPath 'query'");
        continue;
      }
    }

    if (xmlXPathNodeSetIsEmpty (media_template_xpath->nodesetval)) {
      GRL_XML_DEBUG (data->source,
                     GRL_XML_DEBUG_PROVIDE,
                     "Failed: XPath '%s' return no values",
                     xpath);
      xmlXPathFreeObject (media_template_xpath);
      expandable_string_free_value (xpath_query, xpath);
      continue;
    }

    expandable_string_free_value (xpath_query, xpath);

    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Using template in line %ld",
                   media_template->line_number);
    matching_templates = g_list_prepend (matching_templates, media_template);
    matching_xpath = g_list_prepend (matching_xpath,
                                     dataref_new (media_template_xpath,
                                                  (GDestroyNotify) xmlXPathFreeObject));
    data->total_results += media_template_xpath->nodesetval->nodeNr;
    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Obtained %d results",
                   media_template_xpath->nodesetval->nodeNr);
  }

  matching_templates = g_list_reverse (matching_templates);
  matching_xpath = g_list_reverse (matching_xpath);

  pt = matching_templates;
  px = matching_xpath;

  /* Skip results */
  GRL_XML_DEBUG (data->source,
                 GRL_XML_DEBUG_PROVIDE,
                 "Skipping %d results",
                 data->skip);

  /* There are no more elements */
  if (data->total_results <= data->skip) {
    GRL_XML_DEBUG_LITERAL (data->source,
                           GRL_XML_DEBUG_PROVIDE,
                           "No results to send");
    data->callback (NULL, 0, data->user_data, NULL);
    operation_call_data_free (data);
    g_list_free (matching_templates);
    g_list_free_full (matching_xpath, (GDestroyNotify) dataref_unref);
    dataref_unref (xml_ctx_reffed);
    return FALSE;
  }

  data->total_results -= data->skip;
  data->total_results = MIN (data->total_results, data->count);

  if (data->total_results == 0) {
    operation_call_send_list_run (data);
  } else {
    pending = data->total_results;
    skip = data->skip;
    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Sending %d results",
                   data->total_results);
    while (pending > 0) {
      media_template = (MediaTemplate *) pt->data;
      media_template_xpath_reffed = (DataRef *) px->data;
      media_template_xpath = (xmlXPathObjectPtr) dataref_value (media_template_xpath_reffed);
      for (i = skip; i < media_template_xpath->nodesetval->nodeNr && pending > 0; i++) {
        keys = merge_lists (data->keys, media_template->mandatory_keys);
        send_item = send_item_new ();
        GRL_XML_DEBUG (data->source,
                       GRL_XML_DEBUG_PROVIDE,
                       "Creating %s media",
                       gtype_to_string (media_template->media_type));
        send_item->media = g_object_new (media_template->media_type, NULL);
        send_item->pending_count = g_list_length (keys);
        data->send_list = g_list_append (data->send_list, send_item);

        get_raw_data = get_raw_data_new ();
        get_raw_data->xpath_reffed = dataref_ref (media_template_xpath_reffed);
        get_raw_data->xml_ctx_reffed = dataref_ref (xml_ctx_reffed);
        get_raw_data->xml_doc_reffed = dataref_ref (xml_doc_reffed);
        get_raw_data->node = i;
        get_raw_data->namespace = media_template->namespace;
        get_raw_data->namespace_size = media_template->namespace_size;
        get_raw_data->expand_data = expand_data_ref (data->expand_data);

        get_raw_data_reffed = dataref_new (get_raw_data, (GDestroyNotify) get_raw_data_free);

        /* First insert any private value */
        if (media_template->private_keys) {
          private_keys = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                g_free);
          for (prdata_list = media_template->private_keys;
               prdata_list;
               prdata_list = g_list_next (prdata_list)) {
            prdata = (PrivateData *) prdata_list->data;
            prvalue = get_raw_from_path (data->source, prdata->data, get_raw_data_reffed);
            GRL_XML_DEBUG (data->source,
                           GRL_XML_DEBUG_PROVIDE,
                           "Adding \"%s\" private key: \"%s\"",
                           prdata->name,
                           prvalue);
            g_hash_table_insert (private_keys, g_strdup (prdata->name), prvalue);
          }

          json_data = json_ghashtable_serialize_data (private_keys, NULL);
          grl_data_set_string (GRL_DATA (send_item->media),
                               GRL_METADATA_KEY_PRIVATE_KEYS,
                               json_data);
          g_hash_table_unref (private_keys);
          g_free (json_data);
        }

        /* Now add the keys */
        for (k = keys; k; k = g_list_next (k)) {
          if (grl_data_has_key (GRL_DATA (send_item->media),
                                GRLPOINTER_TO_KEYID (k->data))) {
            send_item->pending_count--;
            operation_call_send_list_run (data);
            continue;
          }
          fetch_data = (FetchData *) g_hash_table_lookup (media_template->keys, k->data);
          if (!fetch_data) {
            send_item->pending_count--;
            operation_call_send_list_run (data);
            continue;
          }

          fetch_item = fetch_item_data_new ();
          fetch_item->op_data = data;
          fetch_item->item = send_item;
          fetch_item->key = GRLPOINTER_TO_KEYID (k->data);

          fetch_data_get (data->source,
                          GRL_XML_DEBUG_PROVIDE,
                          data->source->priv->wc,
                          fetch_data,
                          data->expand_data,
                          data->cancellable,
                          get_raw_from_path,
                          get_raw_data_reffed,
                          (DataFetchedCb) fetch_data_obtained,
                          fetch_item);
        }
        dataref_unref (get_raw_data_reffed);
        g_list_free (keys);
        pending--;
      }
      skip -= MIN (skip, media_template_xpath->nodesetval->nodeNr);
      pt = g_list_next (pt);
      px = g_list_next (px);
    }
  }

  g_list_free (matching_templates);
  g_list_free_full (matching_xpath, (GDestroyNotify) dataref_unref);
  dataref_unref (xml_ctx_reffed);
  dataref_unref (xml_doc_reffed);

  return FALSE;
}

static gboolean
operation_call_send_json_results (OperationCallData *data)
{
  DataRef *get_raw_data_reffed;
  ExpandableString *json_query;
  FetchData *fetch_data;
  FetchItemData *fetch_item;
  GHashTable *private_keys;
  GList *k;
  GList *keys;
  GList *matching_json_path = NULL;
  GList *matching_templates = NULL;
  GList *prdata_list;
  GList *pt;
  GList *px;
  GetRawData *get_raw_data;
  JsonArray *json_array;
  JsonNode *json_found_nodes = NULL;
  JsonNode *root_node;
  MediaTemplate *media_template;
  PrivateData *prdata;
  SendItem *send_item;
  gchar *json_data;
  gchar *json_path;
  gchar *prvalue;
  gint pending;
  guint json_array_length;
  guint skip;
  int i;

  if (operation_call_was_cancelled (data)) {
    return FALSE;
  }

  root_node = json_parser_get_root (data->json_parser);

  /* Scan all media templates, finding those that match the results */
  GRL_XML_DEBUG_LITERAL (data->source,
                         GRL_XML_DEBUG_PROVIDE,
                         "Selecting JSON provide template");
  for (pt = data->source->priv->media_templates;
       pt && data->total_results < (data->skip + data->count);
       pt = g_list_next (pt)) {
    media_template = (MediaTemplate *) pt->data;

    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Testing template in line %ld",
                   media_template->line_number);

    if (media_template->format != FORMAT_JSON) {
      GRL_XML_DEBUG_LITERAL (data->source,
                             GRL_XML_DEBUG_PROVIDE,
                             "Failed: not a JSON template");
      continue;
    }

    if (media_template->operation_id &&
        g_strcmp0 (media_template->operation_id, data->operation->id) != 0) {
      GRL_XML_DEBUG (data->source,
                     GRL_XML_DEBUG_PROVIDE,
                     "Failed: expected operation id '%s', current is '%s'",
                     media_template->operation_id,
                     data->operation->id);
      continue;
    }

    json_found_nodes = NULL;
    json_array = NULL;
    if (data->operation_type == OP_RESOLVE) {
      if (media_template->select) {
        json_path = expandable_string_get_value (media_template->select, data->expand_data);
        /* Special case: "$" represents the root node */
        if (json_path[0] == '$' && json_path[1] == '\0') {
          json_array = json_array_new ();
          json_array_add_element (json_array, json_node_copy (root_node));
        } else {
          json_found_nodes = json_path_query (json_path, root_node, NULL);
        }
        json_query = media_template->select;
      } else {
        GRL_XML_DEBUG_LITERAL (data->source,
                               GRL_XML_DEBUG_PROVIDE,
                               "Failed: no select attribute for this media");
        continue;
      }
    } else {
      if (media_template->query) {
        json_path = expandable_string_get_value (media_template->query, data->expand_data);
        /* Special case: "$" represents the root node */
        if (json_path[0] == '$' && json_path[1] == '\0') {
          json_array = json_array_new ();
          json_array_add_element (json_array, json_node_copy (root_node));
        } else {
          json_found_nodes = json_path_query (json_path, root_node, NULL);
        }
        json_query = media_template->query;
      } else {
          GRL_XML_DEBUG_LITERAL (data->source,
                                 GRL_XML_DEBUG_PROVIDE,
                                 "Failed: no query attribute for this media");
          continue;
      }
    }

    if (!json_found_nodes && !json_array) {
      GRL_XML_DEBUG (data->source,
                     GRL_XML_DEBUG_PROVIDE,
                     "Failed: JSON '%s' return no values",
                     json_path);
      expandable_string_free_value (json_query, json_path);
      continue;
    }

    if (!json_array) {
      json_array = json_node_get_array (json_found_nodes);
    }

    json_array_length = json_array_get_length (json_array);

    if (json_array_length == 0) {
      GRL_XML_DEBUG (data->source,
                     GRL_XML_DEBUG_PROVIDE,
                     "Failed: JSON '%s' return no values",
                     json_path);
      if (json_found_nodes) {
        json_node_free (json_found_nodes);
      } else {
        json_array_unref (json_array);
      }
      expandable_string_free_value (json_query, json_path);
      continue;
    }

    expandable_string_free_value (json_query, json_path);

    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Using template in line %ld",
                   media_template->line_number);

    matching_templates = g_list_prepend (matching_templates, media_template);
    matching_json_path = g_list_prepend (matching_json_path,
                                         json_array_ref (json_array));
    data->total_results += json_array_length;
    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Obtained %d results",
                   json_array_length);

    if (json_found_nodes) {
      json_node_free (json_found_nodes);
    } else {
      json_array_unref (json_array);
    }
  }

  matching_templates = g_list_reverse (matching_templates);
  matching_json_path = g_list_reverse (matching_json_path);

  pt = matching_templates;
  px = matching_json_path;

  /* Skip results */
  GRL_XML_DEBUG (data->source,
                 GRL_XML_DEBUG_PROVIDE,
                 "Skipping %d results",
                 data->skip);

  /* There are no more elements */
  if (data->total_results <= data->skip) {
    GRL_XML_DEBUG_LITERAL (data->source,
                           GRL_XML_DEBUG_PROVIDE,
                           "No results to send");
    data->callback (NULL, 0, data->user_data, NULL);
    operation_call_data_free (data);
    g_list_free (matching_templates);
    g_list_free_full (matching_json_path, (GDestroyNotify) json_array_unref);
    return FALSE;
  }

  data->total_results -= data->skip;
  data->total_results = MIN (data->total_results, data->count);

  if (data->total_results == 0) {
    operation_call_send_list_run (data);
  } else {
    pending = data->total_results;
    skip = data->skip;
    GRL_XML_DEBUG (data->source,
                   GRL_XML_DEBUG_PROVIDE,
                   "Sending %d results",
                   data->total_results);
    while (pending > 0) {
      media_template = (MediaTemplate *) pt->data;
      json_array = (JsonArray *) px->data;
      json_array_length = json_array_get_length (json_array);
      for (i = skip; i < json_array_length && pending > 0; i++) {
        keys = merge_lists (data->keys, media_template->mandatory_keys);
        send_item = send_item_new ();
        GRL_XML_DEBUG (data->source,
                       GRL_XML_DEBUG_PROVIDE,
                       "Creating %s media",
                       gtype_to_string (media_template->media_type));
        send_item->media = g_object_new (media_template->media_type, NULL);
        send_item->pending_count = g_list_length (keys);
        data->send_list = g_list_append (data->send_list, send_item);

        get_raw_data = get_raw_data_new ();
        get_raw_data->json_array = json_array_ref (json_array);
        get_raw_data->node = i;
        get_raw_data->expand_data = expand_data_ref (data->expand_data);

        get_raw_data_reffed = dataref_new (get_raw_data, (GDestroyNotify) get_raw_data_free);

        /* First insert any private value */
        if (media_template->private_keys) {
          private_keys = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                NULL,
                                                g_free);
          for (prdata_list = media_template->private_keys;
               prdata_list;
               prdata_list = g_list_next (prdata_list)) {
            prdata = (PrivateData *) prdata_list->data;
            prvalue = get_raw_from_path (data->source, prdata->data, get_raw_data_reffed);
            GRL_XML_DEBUG (data->source,
                           GRL_XML_DEBUG_PROVIDE,
                           "Adding \"%s\" private key: \"%s\"",
                           prdata->name,
                         prvalue);
            g_hash_table_insert (private_keys, prdata->name, prvalue);
          }

          json_data = json_ghashtable_serialize_data (private_keys, NULL);
          grl_data_set_string (GRL_DATA (send_item->media),
                               GRL_METADATA_KEY_PRIVATE_KEYS,
                               json_data);
          g_hash_table_unref (private_keys);
          g_free (json_data);
        }

        /* Now add the keys */
        for (k = keys; k; k = g_list_next (k)) {
          if (grl_data_has_key (GRL_DATA (send_item->media),
                                GRLPOINTER_TO_KEYID (k->data))) {
            send_item->pending_count--;
            operation_call_send_list_run (data);
            continue;
          }
          if (data->operation_type != OP_RESOLVE &&
              g_list_find (data->source->priv->use_resolve_keys, k->data)) {
            send_item->apply_resolve = TRUE;
            send_item->pending_count--;
            operation_call_send_list_run (data);
            continue;
          }
          fetch_data = (FetchData *) g_hash_table_lookup (media_template->keys, k->data);
          if (!fetch_data) {
            send_item->pending_count--;
            operation_call_send_list_run (data);
            continue;
          }

          fetch_item = fetch_item_data_new ();
          fetch_item->op_data = data;
          fetch_item->item = send_item;
          fetch_item->key = GRLPOINTER_TO_KEYID (k->data);

          fetch_data_get (data->source,
                          GRL_XML_DEBUG_PROVIDE,
                          data->source->priv->wc,
                          fetch_data,
                          data->expand_data,
                          data->cancellable,
                          get_raw_from_path,
                          get_raw_data_reffed,
                          (DataFetchedCb) fetch_data_obtained,
                          fetch_item);
        }
        dataref_unref (get_raw_data_reffed);
        g_list_free (keys);
        pending--;
      }
      skip -= MIN (skip, json_array_length);
      pt = g_list_next (pt);
      px = g_list_next (px);
    }
  }

  g_list_free (matching_templates);
  g_list_free_full (matching_json_path, (GDestroyNotify) json_array_unref);

  return FALSE;
}

static void
operation_call_data_fetched (const gchar *content,
                             OperationCallData *data,
                             const GError *op_error)
{
  GError *error = NULL;
  JsonParser *json_parser;
  gboolean parser_success = FALSE;
  xmlDocPtr xml_doc = NULL;

  if (op_error) {
    data->callback (NULL, 0, data->user_data, error);
    operation_call_data_free (data);
    return;
  }

  if (operation_call_was_cancelled (data)) {
    return;
  }

  if (data->operation->result->format == FORMAT_XML) {
    xml_doc = xmlReadMemory (content, xmlStrlen ((const xmlChar *) content), NULL, NULL,
                             XML_PARSE_RECOVER | XML_PARSE_NOBLANKS);
  } else {
    json_parser = json_parser_new ();
    parser_success = json_parser_load_from_data (json_parser, content, -1, NULL);
  }

  if (!xml_doc && !parser_success) {
    error = g_error_new (GRL_CORE_ERROR, 0, "Unable to read source: can't parse result");
    GRL_DEBUG ("%s", error->message);
    data->callback (NULL, 0, data->user_data, error);
    g_error_free (error);
    operation_call_data_free (data);
    return;
  }

  if (data->operation->result->format == FORMAT_XML) {
    data->xml_doc_reffed = dataref_new (xml_doc, (GDestroyNotify) xmlFreeDoc);
  } else {
    data->json_parser = json_parser;
  }

  /* Cache results if proceed */
  if (data->operation->result->cache_time > 0) {
    if (!data->operation->result->cache_valid &&
        data->operation->result->cache.xml) {
      if (data->operation->result->format == FORMAT_XML) {
        dataref_unref (data->operation->result->cache.xml);
      } else {
        g_object_unref (data->operation->result->cache.json);
      }
    }
    if (data->operation->result->format == FORMAT_XML) {
      data->operation->result->cache.xml = dataref_ref (data->xml_doc_reffed);
    } else {
      data->operation->result->cache.json = g_object_ref (data->json_parser);
    }
    data->operation->result->cache_valid = TRUE;
    g_timeout_add_seconds (data->operation->result->cache_time,
                           (GSourceFunc) cache_expired_cb,
                           data->operation->result);
  }
  if (data->operation->result->format == FORMAT_XML) {
    EXECUTE_CALL (data->options,
                  operation_call_send_xml_results,
                  data);
  } else {
    EXECUTE_CALL (data->options,
                  operation_call_send_json_results,
                  data);
  }
}

static void
operation_call (OperationCallData *data)
{
  DataRef *data_reffed;

  data->skip = expandable_string_to_number (data->operation->skip,
                                            data->expand_data,
                                            0);
  data->count = expandable_string_to_number (data->operation->count,
                                             data->expand_data,
                                             data->operation_type == OP_RESOLVE? 1: G_MAXUINT);

  /* Avoid trying to send more elements than requested */
  data->count = MIN (data->count, grl_operation_options_get_count (data->options));

  if (data->operation->result->cache.xml &&
      data->operation->result->cache_valid) {
    GRL_XML_DEBUG_LITERAL (data->source,
                           GRL_XML_DEBUG_PROVIDE,
                           "Reusing cached result");

    if (data->operation->result->format == FORMAT_XML) {
      data->xml_doc_reffed = dataref_ref (data->operation->result->cache.xml);
      EXECUTE_CALL (data->options,
                    operation_call_send_xml_results,
                    data);
    } else {
      data->json_parser = g_object_ref (data->operation->result->cache.json);
      EXECUTE_CALL (data->options,
                    operation_call_send_json_results,
                    data);
    }
  } else {
    data_reffed = dataref_new (expand_data_ref (data->expand_data),
                             (GDestroyNotify) expand_data_unref);
    fetch_data_get (data->source,
                    GRL_XML_DEBUG_OPERATION,
                    data->source->priv->wc,
                    data->operation->result->query,
                    data->expand_data,
                    data->cancellable,
                    get_raw_from_operation,
                    data_reffed,
                    (DataFetchedCb) operation_call_data_fetched,
                    data);
    dataref_unref (data_reffed);
  }
}

/* Returns %TRUE if the @container matches with the requeriments for @operation;
   if it can't decide due lack of information, it will return the missing keys
   in @missing_keys */
static gboolean
operation_requirements_match (GrlXmlFactorySource *factory_source,
                              Operation *operation,
                              GrlMedia *media,
                              GList **missing_keys)
{
  OperationRequirement *req;
  GList *req_list;
  const gchar *key_value;

  GRL_XML_DEBUG (factory_source,
                 GRL_XML_DEBUG_OPERATION,
                 "    Testing operation in line %ld",
                 operation->line_number);

  /* Check type */
  if (media &&
      !G_TYPE_CHECK_INSTANCE_TYPE (media, operation->required_type)) {
    GRL_XML_DEBUG (factory_source,
                   GRL_XML_DEBUG_OPERATION,
                   "      Failed: required %s media, but current media is %s",
                   gtype_to_string (operation->required_type),
                   gtype_to_string (G_TYPE_FROM_INSTANCE (media)));
    return FALSE;
  }

  /* Check each key */
  req_list = operation->requirements;
  while (req_list) {
    req = (OperationRequirement *) req_list->data;
    if (media) {
      key_value = grl_data_get_string (GRL_DATA (media), req->key);
      if (!key_value) {
        if (req->match_reg) {
          key_value = "";
        } else {
          if (missing_keys) {
            *missing_keys = g_list_prepend (*missing_keys, GRLKEYID_TO_POINTER (req->key));
          } else {
            return FALSE;
          }
        }
      }

      if (key_value &&
          req->match_reg &&
          !g_regex_match (req->match_reg, key_value, 0, NULL)) {
        GRL_XML_DEBUG (factory_source,
                       GRL_XML_DEBUG_OPERATION,
                       "      Checking if '%s' key value ('%s') matches '%s': failed",
                       grl_metadata_key_get_name (req->key),
                       key_value,
                       g_regex_get_pattern (req->match_reg));

        if (missing_keys) {
          g_list_free (*missing_keys);
          *missing_keys = NULL;
        }
        return FALSE;
      } else {
        GRL_XML_DEBUG (factory_source,
                       GRL_XML_DEBUG_OPERATION,
                       "      Checking if '%s' key value ('%s') matches '%s': succeed",
                       grl_metadata_key_get_name (req->key),
                       key_value,
                       req->match_reg? g_regex_get_pattern (req->match_reg): "");
      }
    } else {
      if (missing_keys) {
        *missing_keys = g_list_prepend (*missing_keys, GRLKEYID_TO_POINTER (req->key));
      } else {
        return FALSE;
      }
    }
    req_list = g_list_next (req_list);
  }
  if (!media) {
      return FALSE;
  }

  GRL_XML_DEBUG (factory_source,
                 GRL_XML_DEBUG_OPERATION,
                 "    Using operation in line %ld",
                 operation->line_number);
  return TRUE;
}

/* Selects the browse operation that matches the current container */
static Operation *
browse_select_operation (GrlSource *source,
                         GrlMedia *container)
{
  GList *operation_list;
  GrlXmlFactorySource *factory_source;

  factory_source = GRL_XML_FACTORY_SOURCE (source);
  GRL_XML_DEBUG_LITERAL (factory_source,
                         GRL_XML_DEBUG_OPERATION,
                         "  Selecting browse operation");
  operation_list = (GList *) factory_source->priv->operations[OP_BROWSE];
  while (operation_list) {
    if (operation_requirements_match (factory_source, operation_list->data, container, NULL)) {
      break;
    }
    operation_list = g_list_next (operation_list);
  }

  /* Nothing matches; send no results */
  if (!operation_list) {
    return NULL;
  } else {
    return operation_list->data;
  }
}

static Operation *
resolve_select_operation (GrlSource *source,
                          GrlMedia *media)
{
  GList *operation_list;
  GrlXmlFactorySource *factory_source;

  factory_source = GRL_XML_FACTORY_SOURCE (source);
  operation_list = (GList *) factory_source->priv->operations[OP_RESOLVE];
  while (operation_list) {
    if (operation_requirements_match (factory_source, operation_list->data, media, NULL)) {
      break;
    }
    operation_list = g_list_next (operation_list);
  }

  /* Nothing matches */
  if (!operation_list) {
    return NULL;
  } else {
    return operation_list->data;
  }
}

static OperationCallData *
get_resolve_data (GrlXmlFactorySource *source,
                  GCancellable *cancellable,
                  GrlMedia *media,
                  GList *keys,
                  GrlOperationOptions *options,
                  SendResultCb callback,
                  gpointer user_data)
{
  OperationCallData *data = NULL;
  Operation *operation;

  operation = resolve_select_operation (GRL_SOURCE (source), media);
  if (!operation) {
    return FALSE;
  }

  data = operation_call_data_new ();
  data->cancellable = cancellable;
  data->source = source;
  data->operation = operation;
  data->operation_type = OP_RESOLVE;
  data->options = options;
  data->user_data = user_data;
  data->callback = callback;
  data->keys = keys;
  data->expand_data = expand_data_new (data->source, media, NULL, data->options);

  return data;
}

/* ================== API Implementation ================ */

gboolean
grl_xml_factory_source_is_debug (GrlXmlFactorySource *source,
                                 GrlXmlDebug flag)
{
  return (source->priv->debug & flag);
}

gchar *
grl_xml_factory_source_run_script (GrlXmlFactorySource *source,
                                   const gchar *script,
                                   GError **error)
{
  gchar *result;
  int status;

  if (!source->priv->lua_state) {
    source->priv->lua_state = luaL_newstate ();
    luaL_openlibs (source->priv->lua_state);
  }

  status = luaL_loadstring (source->priv->lua_state, script);
  if (!status) {
    status = lua_pcall (source->priv->lua_state, 0, 1, 0);
  }
  if (status) {
    g_set_error (error,
                 GRL_CORE_ERROR,
                 0,
                 "Cannot run script: %s",
                 lua_tostring (source->priv->lua_state, -1));
    lua_pop (source->priv->lua_state, 1);
    return NULL;
  }

  result = g_strdup (lua_tostring (source->priv->lua_state, -1));
  lua_pop (source->priv->lua_state, 1);
  return result;
}

static const GList *
grl_xml_factory_source_supported_keys (GrlSource *source)
{
  GrlXmlFactorySource *factory_source = GRL_XML_FACTORY_SOURCE (source);
  return factory_source->priv->supported_keys;
}

static const GList *
grl_xml_factory_source_slow_keys (GrlSource *source)
{
  GrlXmlFactorySource *factory_source = GRL_XML_FACTORY_SOURCE (source);
  return factory_source->priv->slow_keys;
}


static GrlSupportedOps
grl_xml_factory_source_supported_operations (GrlSource *source)
{
  GrlSupportedOps caps = 0;
  GrlXmlFactorySource *factory_source = GRL_XML_FACTORY_SOURCE (source);

  if (factory_source->priv->operations[OP_SEARCH]) {
    caps |= GRL_OP_SEARCH;
  }

  if (factory_source->priv->operations[OP_BROWSE]) {
    caps |= GRL_OP_BROWSE;
  }

  if (factory_source->priv->operations[OP_RESOLVE]) {
    caps |= GRL_OP_RESOLVE;
  }

  return caps;
}

static void
grl_xml_factory_source_browse (GrlSource *source,
                               GrlSourceBrowseSpec *bs)
{
  OperationCallData *data;
  Operation *operation;

  GRL_XML_DEBUG (GRL_XML_FACTORY_SOURCE (source),
                 GRL_XML_DEBUG_OPERATION,
                 "Browsing '%s' container (skip: %u, count: %d)",
                 grl_media_serialize (bs->container),
                 grl_operation_options_get_skip (bs->options),
                 grl_operation_options_get_count (bs->options));

  operation = browse_select_operation (bs->source, bs->container);
  if (!operation) {
    GRL_XML_DEBUG_LITERAL (GRL_XML_FACTORY_SOURCE (source),
                           GRL_XML_DEBUG_OPERATION,
                           "No suitable browse operation found");
    bs->callback (bs->source, bs->operation_id, NULL, 0, bs->user_data, NULL);
    return;
  }

  data = operation_call_data_new ();
  data->cancellable = g_cancellable_new ();
  data->source = GRL_XML_FACTORY_SOURCE (source);
  data->operation = operation;
  data->operation_type = OP_BROWSE;
  data->options = bs->options;
  data->user_data = bs;
  data->callback = (SendResultCb) browse_send_result_cb;
  data->keys = bs->keys;
  data->expand_data = expand_data_new (data->source, bs->container, NULL, data->options);

  grl_operation_set_data (bs->operation_id, data->cancellable);

  operation_call (data);
}

static void
grl_xml_factory_source_search (GrlSource *source,
                               GrlSourceSearchSpec *ss)
{
  OperationCallData *data;

  GRL_XML_DEBUG (GRL_XML_FACTORY_SOURCE (source),
                 GRL_XML_DEBUG_OPERATION,
                 "Searching '%s' text (skip: %u, count: %d)",
                 ss->text ? ss->text: "",
                 grl_operation_options_get_skip (ss->options),
                 grl_operation_options_get_count (ss->options));

  data = operation_call_data_new ();
  data->cancellable = g_cancellable_new ();
  data->source = GRL_XML_FACTORY_SOURCE (source);
  data->operation = (Operation *) data->source->priv->operations[OP_SEARCH]->data;
  data->operation_type = OP_SEARCH;
  data->options = ss->options;
  data->user_data = ss;
  data->callback = (SendResultCb) search_send_result_cb;
  data->keys = ss->keys;
  data->expand_data = expand_data_new (data->source, NULL, ss->text, data->options);

  grl_operation_set_data (ss->operation_id, data->cancellable);

  operation_call (data);
}

static void
grl_xml_factory_source_resolve (GrlSource *source,
                                GrlSourceResolveSpec *rs)
{
  GCancellable *cancellable;
  OperationCallData *data;

  GRL_XML_DEBUG (GRL_XML_FACTORY_SOURCE (source),
                 GRL_XML_DEBUG_PROVIDE,
                 "Resolving '%s' media",
                 grl_media_serialize (rs->media));

  cancellable = g_cancellable_new ();
  data = get_resolve_data (GRL_XML_FACTORY_SOURCE (rs->source),
                           cancellable,
                           rs->media,
                           rs->keys,
                           rs->options,
                           (SendResultCb) resolve_send_result_cb,
                           rs);

  if (!data) {
    g_object_unref (cancellable);
    rs->callback (rs->source, rs->operation_id, rs->media, rs->user_data, NULL);
    return;
  }

  grl_operation_set_data (rs->operation_id, data->cancellable);
  operation_call (data);
}

static gboolean
grl_xml_factory_source_may_resolve (GrlSource *source,
                                    GrlMedia *media,
                                    GrlKeyID key_id,
                                    GList **missing_keys)
{
  GList *operation_list = NULL;
  GrlXmlFactorySource *factory_source;
  Operation *operation;

  factory_source = GRL_XML_FACTORY_SOURCE (source);

  if (!g_list_find (factory_source->priv->supported_keys, GRLKEYID_TO_POINTER (key_id))) {
    return FALSE;
  }

  for (operation_list = (GList *) factory_source->priv->operations[OP_RESOLVE];
       operation_list;
       operation_list = g_list_next (operation_list)) {
    operation = (Operation *) operation_list->data;
    if (operation->resolve_key != GRL_METADATA_KEY_INVALID &&
        operation->resolve_key != key_id) {
      continue;
    }

    if (media &&
        !operation->resolve_any &&
        g_strcmp0 (grl_media_get_source (media), grl_source_get_id (source)) != 0) {
      return FALSE;
    }

    /* This could match. Let's check if we have everything to confirm */
    if (operation_requirements_match (factory_source, operation, media, missing_keys)) {
      return TRUE;
    } else {
      if (*missing_keys) {
        return FALSE;
      } else {
        continue;
      }
    }
  }

  return FALSE;
}

static void
grl_xml_factory_source_cancel (GrlSource *source,
                               guint operation_id)
{
  GCancellable *cancellable;

  cancellable = (GCancellable *) grl_operation_get_data (operation_id);

  if (cancellable) {
    g_cancellable_cancel (cancellable);
  }
}
