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
test_xml_factory_regexp_full (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-regexp-full");
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

  g_assert_cmpstr (grl_media_get_id (media), ==, "My Id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "Your Testing 'Title'");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_regexp_no_expression (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-regexp-no-expression");
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

  g_assert_cmpstr (grl_media_get_id (media), ==, "My Id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "This Is My Testing Title Twice");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_regexp_no_output (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-regexp-no-output");
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

  g_assert_cmpstr (grl_media_get_id (media), ==, "My Id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "My");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_regexp_no_input (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-regexp-no-input");
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
  g_assert_cmpstr (grl_media_get_id (media), ==, "My Id");
  g_assert (!grl_media_audio_get_artist (GRL_MEDIA_AUDIO (media)));
  g_assert_cmpstr(grl_media_get_title (media), ==, "This is a fixed title");

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

static void
test_xml_factory_regexp_repeat_expression (void)
{
  GError *error = NULL;
  GList *medias;
  GrlMedia *media;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-regexp-repeat-expression");
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

  g_assert_cmpstr (grl_media_get_id (media), ==, "My Id");
  g_assert_cmpstr (grl_media_get_title (media),
                   ==,
                   "My and Testing and ");

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

  g_test_add_func ("/xml-factory/regexp/full", test_xml_factory_regexp_full);
  g_test_add_func ("/xml-factory/regexp/no-expression", test_xml_factory_regexp_no_expression);
  g_test_add_func ("/xml-factory/regexp/no-output", test_xml_factory_regexp_no_output);
  g_test_add_func ("/xml-factory/regexp/no-input", test_xml_factory_regexp_no_input);
  g_test_add_func ("/xml-factory/regexp/repeat-expression", test_xml_factory_regexp_repeat_expression);

  return g_test_run ();
}
