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
  grl_registry_load_all_plugins (registry, TRUE, &error);
  g_assert_no_error (error);
}

static void
test_xml_factory_replace_replace (void)
{
  GError *error = NULL;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-replace");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  g_assert (options);

  media = grl_media_audio_new ();
  grl_media_set_source (media, "xml-test-replace");
  grl_media_set_id (media, "replace");

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);

  g_assert (media);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_get_id (media), ==, "replace");
  g_assert_cmpstr (grl_media_get_artist (media),
                   ==,
                   "My Artist");
  g_assert_cmpstr (grl_media_get_album (media),
                   ==,
                   "This Album");

  g_object_unref (media);
  g_object_unref (options);
}

static void
test_xml_factory_replace_remove (void)
{
  GError *error = NULL;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-replace");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  g_assert (options);

  media = grl_media_audio_new ();
  grl_media_set_source (media, "xml-test-replace");
  grl_media_set_id (media, "remove");

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);

  g_assert (media);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_get_id (media), ==, "remove");
  g_assert_cmpstr (grl_media_get_artist (media), ==, "Artist");
  g_assert_cmpstr (grl_media_get_title (media), ==, "Title");

  g_object_unref (media);
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

  g_test_add_func ("/xml-factory/replace/replace", test_xml_factory_replace_replace);
  g_test_add_func ("/xml-factory/replace/remove", test_xml_factory_replace_remove);

  return g_test_run ();
}
