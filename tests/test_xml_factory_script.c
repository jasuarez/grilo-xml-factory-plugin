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
test_xml_factory_script_init_success (void)
{
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-script-init-success");
  g_assert (source);
}

static void
test_xml_factory_script_init_fail (void)
{
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-script-init-fail");
  g_assert (!source);
}

static void
test_xml_factory_script_return_string (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-script-init-success");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  medias = grl_source_search_sync (source,
                                   "string",
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_cmpint (g_list_length (medias), ==, 1);
  g_assert_no_error (error);

  media = (GrlMedia *) medias->data;

  g_assert_cmpstr (grl_media_get_id (media),
                   ==,
                   "1");
  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "John Doe");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "title foo");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_script_return_number (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-script-init-success");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  medias = grl_source_search_sync (source,
                                   "number",
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_cmpint (g_list_length (medias), ==, 1);
  g_assert_no_error (error);

  media = (GrlMedia *) medias->data;

  g_assert_cmpstr (grl_media_get_id (media),
                   ==,
                   "1");
  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "John Doe");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "42");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_script_return_invalid (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-script-init-success");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  medias = grl_source_search_sync (source,
                                   "invalid",
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_cmpint (g_list_length (medias), ==, 1);
  g_assert_no_error (error);

  media = (GrlMedia *) medias->data;

  g_assert_cmpstr (grl_media_get_id (media),
                   ==,
                   "1");
  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "John Doe");
  g_assert (!grl_media_get_title (media));

  g_list_free_full (medias, g_object_unref);
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

  g_test_add_func ("/xml-factory/script/init-success", test_xml_factory_script_init_success);
  g_test_add_func ("/xml-factory/script/init-fail", test_xml_factory_script_init_fail);
  g_test_add_func ("/xml-factory/script/return-string", test_xml_factory_script_return_string);
  g_test_add_func ("/xml-factory/script/return-number", test_xml_factory_script_return_number);
  g_test_add_func ("/xml-factory/script/return-invalid", test_xml_factory_script_return_invalid);

  return g_test_run ();
}
