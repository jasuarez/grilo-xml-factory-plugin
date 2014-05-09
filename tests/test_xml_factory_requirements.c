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

static GMainLoop *main_loop = NULL;

static void
test_xml_factory_setup (void)
{
  GError *error = NULL;
  GrlRegistry *registry;

  registry = grl_registry_get_default ();
  grl_registry_load_all_plugins (registry, &error);
  g_assert_no_error (error);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_assert (main_loop);
}

static void
test_xml_factory_requirements_no_check (void)
{
  GError *error = NULL;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-requirements");
  g_assert (source);
  options = grl_operation_options_new (NULL);

  media = grl_media_audio_new ();
  grl_media_audio_set_artist (GRL_MEDIA_AUDIO (media), "John Doe");

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "John Doe");
  g_assert_cmpstr (grl_media_get_title (GRL_MEDIA (media)),
                   ==,
                   "Artist is John Doe");

  g_object_unref (media);
  g_object_unref (options);
}

static void
test_xml_factory_requirements_check (void)
{
  GError *error = NULL;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-requirements");
  g_assert (source);
  options = grl_operation_options_new (NULL);

  media = grl_media_audio_new ();
  grl_media_audio_set_artist (GRL_MEDIA_AUDIO (media), "Special Doe");

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "Special Doe");

  /* No special match due lack of bitrate */
  g_assert_cmpstr (grl_media_get_title (GRL_MEDIA (media)),
                   ==,
                   "Artist is Special Doe");

  grl_data_remove (GRL_DATA (media), GRL_METADATA_KEY_TITLE);
  grl_media_audio_set_bitrate (GRL_MEDIA_AUDIO (media), 128);

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "Special Doe");

  /* No special match due bitrate not matching 2?? */
  g_assert_cmpstr (grl_media_get_title (GRL_MEDIA (media)),
                   ==,
                   "Artist is Special Doe");

  grl_data_remove (GRL_DATA (media), GRL_METADATA_KEY_TITLE);
  grl_media_audio_set_bitrate (GRL_MEDIA_AUDIO (media), 256);

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "Special Doe");

  /* Special match due bitrate matching 2?? */
  g_assert_cmpstr (grl_media_get_title (GRL_MEDIA (media)),
                   ==,
                   "Artist is Special Doe (check matches)");

  g_object_unref (media);
  g_object_unref (options);
}

static void
test_xml_factory_requirements_no_match (void)
{
  GError *error = NULL;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-requirements");
  g_assert (source);
  options = grl_operation_options_new (NULL);

  media = grl_media_audio_new ();
  grl_media_audio_set_album (GRL_MEDIA_AUDIO (media), "An album");

  media = grl_source_resolve_sync (source,
                                   media,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);
  g_assert_no_error (error);

  g_assert_cmpstr (grl_media_audio_get_album (GRL_MEDIA_AUDIO (media)),
                   ==,
                   "An album");
  g_assert (!grl_media_get_title (GRL_MEDIA (media)));

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

  g_test_add_func ("/xml-factory/requirements/no-check", test_xml_factory_requirements_no_check);
  g_test_add_func ("/xml-factory/requirements/check", test_xml_factory_requirements_check);
  g_test_add_func ("/xml-factory/requirements/no-match", test_xml_factory_requirements_no_match);

  return g_test_run ();
}
