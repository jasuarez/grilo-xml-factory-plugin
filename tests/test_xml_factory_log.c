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
test_xml_factory_log (void)
{
  GError *error = NULL;
  GList *medias;
  GrlOperationOptions *options;
  GrlRegistry *registry;
  GrlSource *source;

  registry = grl_registry_get_default ();
  source = grl_registry_lookup_source (registry, "xml-test-log");
  g_assert (source);
  options = grl_operation_options_new (NULL);
  g_assert (options);

  /* Expected messages from <operation> section */
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Browsing * container (skip: *, count: *)");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: * Selecting browse operation");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: * Testing operation *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: * Using operation *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Use *");

  /* Expected messages from <provide> section */
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Selecting XML provide template");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Testing template *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Using template *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Obtained * results");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Skipping * results");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Sending * results");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Creating * media");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Use *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Adding * key: *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Use *");
  g_test_expect_message ("Grilo",
                         G_LOG_LEVEL_DEBUG,
                         "[xml-factory] xml-test-log: Adding * key: *");

  medias = grl_source_browse_sync (source,
                                   NULL,
                                   grl_source_supported_keys (source),
                                   options,
                                   &error);

  g_test_assert_expected_messages ();

  g_list_free_full (medias, g_object_unref);
  g_object_unref (options);
}

int
main(int argc, char **argv)
{
  g_setenv ("GRL_PLUGIN_PATH", XML_FACTORY_PLUGIN_PATH, TRUE);
  g_setenv ("GRL_PLUGIN_LIST", XML_FACTORY_ID, TRUE);
  g_setenv ("GRL_XML_FACTORY_SPECS_PATH", XML_FACTORY_SPECS_PATH, TRUE);
  g_setenv ("GRL_DEBUG", "xml-factory:*", TRUE);

  grl_init (&argc, &argv);
  g_test_init (&argc, &argv, NULL);

#if !GLIB_CHECK_VERSION(2,32,0)
  g_thread_init (NULL);
#endif

  test_xml_factory_setup ();

  g_test_add_func ("/xml-factory/log", test_xml_factory_log);

  return g_test_run ();
}
