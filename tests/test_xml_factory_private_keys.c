/*
 * Copyright (C) 2013 Igalia S.L.
 *
 * Author: Juan A. Suarez Romero <jasuarez@igalia.com>
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

#include <grilo.h>

#define XML_FACTORY_ID "grl-xml-factory"

static void
test_xml_factory_setup (void)
{
  GError *error = NULL;
  GrlRegistry *registry;

  registry = grl_registry_get_default ();
  grl_registry_load_all_plugins (registry, &error);
  g_assert_no_error (error);
}

static void
test_xml_factory_private_keys (void)
{
  GError *error = NULL;
  GList *medias;
  GList *medias_searched;
  GrlKeyID private_keys_key;
  GrlMedia *media;
  GrlMedia *media_searched;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-private-keys");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  g_assert (options);

  medias = grl_source_browse_sync (source,
                                   NULL,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_cmpint (g_list_length(medias), ==, 1);
  g_assert_no_error (error);

  media = (GrlMedia *) medias->data;

  g_assert_cmpstr (grl_media_get_id (media), ==, "id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "My Title");

  private_keys_key = grl_registry_lookup_metadata_key (registry,
                                                       "xml-factory-private-keys");
  g_assert (private_keys_key);
  g_assert_cmpstr (grl_data_get_string (GRL_DATA (media), private_keys_key),
                   ==,
                   "{\"xml-test-private-keys::pr\":\"My private value\"}");

  grl_source_resolve_sync (source,
                           media,
                           grl_source_supported_keys (source),
                           options,
                           &error);

  g_assert_cmpstr (grl_media_get_id (media), ==, "id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "My Title");
  g_assert_cmpstr (grl_data_get_string (GRL_DATA (media), GRL_METADATA_KEY_ARTIST),
                   ==,
                   "My artist named 'My private value'");
  g_assert_cmpstr (grl_data_get_string (GRL_DATA (media), private_keys_key),
                   ==,
                   "{\"xml-test-private-keys::privfield\":\"Another private value\",\"xml-test-private-keys::pr\":\"My private value\"}");

  medias_searched = grl_source_search_sync (source,
                                            NULL,
                                            grl_source_supported_keys (source),
                                            options,
                                            &error);
  g_assert_cmpint (g_list_length(medias), ==, 1);
  g_assert_no_error (error);

  media_searched = (GrlMedia *) medias_searched->data;

  g_assert_cmpstr (grl_media_get_id (media_searched), ==, "id");
  g_assert_cmpstr (grl_media_get_title (media_searched),
                   ==,
                   "Search result title");

  g_assert_cmpstr (grl_data_get_string (GRL_DATA (media_searched), private_keys_key),
                   ==,
                   "{\"xml-test-private-keys::pr\":\"Search result private value\"}");

  g_list_free_full (medias, g_object_unref);
  g_list_free_full (medias_searched, g_object_unref);
  g_object_unref (options);
}

int
main(int argc, char **argv)
{
  g_setenv ("GRL_PLUGIN_PATH", XML_FACTORY_PLUGIN_PATH, TRUE);
  g_setenv ("GRL_PLUGIN_LIST", XML_FACTORY_ID, TRUE);
  g_setenv ("GRL_XML_FACTORY_SPECS_PATH", XML_FACTORY_SPECS_PATH, TRUE);

  grl_init (&argc, &argv);
  g_test_init (&argc, &argv, NULL);

#if !GLIB_CHECK_VERSION(2,32,0)
  g_thread_init (NULL);
#endif

  test_xml_factory_setup ();

  g_test_add_func ("/xml-factory/private-keys", test_xml_factory_private_keys);

  return g_test_run ();
}
