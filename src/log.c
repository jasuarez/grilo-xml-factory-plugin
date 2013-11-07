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

#include "log.h"

#include <string.h>

LogDumpData *
log_dump_data_new (GrlXmlFactorySource * source,
                   guint line,
                   const gchar *dumpfile)
{
  GError *error = NULL;
  GFileOutputStream *file_stream;
  LogDumpData *data;

  data = g_slice_new (LogDumpData);
  data->source_id = (gchar *) grl_source_get_id (GRL_SOURCE (source));
  data->line = line;
  data->dump_file = g_file_new_for_path (dumpfile);
  file_stream = g_file_append_to (data->dump_file,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  &error);
  if (error) {
    GRL_WARNING ("Unable to append to %s: %s",
                 dumpfile,
                 error->message);
    g_clear_object (&data->dump_file);
    data->dump_stream = NULL;
  } else {
    data->dump_stream = G_OUTPUT_STREAM (file_stream);
  }

  return data;
}

void
log_dump_data_free (LogDumpData *data)
{
  if (!data) {
    return;
  }

  g_clear_object (&data->dump_stream);
  g_clear_object (&data->dump_file);
  g_slice_free (LogDumpData, data);
}

void
grl_xml_debug_valist (GrlXmlFactorySource *source,
                      const gchar *format,
                      va_list varargs)
{
  gchar *message;

  message = g_strdup_vprintf (format, varargs);
  grl_xml_debug_literal (source, message);
  g_free (message);
}

void
grl_xml_debug (GrlXmlFactorySource *source,
               const gchar *format,
               ...)
{
  va_list varargs;

  va_start (varargs, format);
  grl_xml_debug_valist (source, format, varargs);
  va_end (varargs);
}

void
grl_xml_debug_literal (GrlXmlFactorySource *source,
                       const gchar *literal)
{
  grl_log (xml_factory_log_domain,
           GRL_LOG_LEVEL_DEBUG,
           grl_source_get_id (GRL_SOURCE (source)),
           literal);
}

void
grl_xml_dump (LogDumpData *data,
              const gchar *dump,
              gsize length)
{
  GDateTime *current_date;
  const gchar *suffix = "]]]END\n";
  gchar *prefix;
  gchar *timestamp;

  current_date = g_date_time_new_now_local ();
  timestamp = g_date_time_format (current_date, "%c");
  prefix = g_strdup_printf ("%s %s[%d]: BEGIN[[[",
                            timestamp,
                            data->source_id,
                            data->line);
  g_date_time_unref (current_date);
  g_free (timestamp);

  g_output_stream_write (data->dump_stream,
                         prefix,
                         strlen (prefix),
                         NULL,
                         NULL);
  g_output_stream_write (data->dump_stream,
                         dump,
                         length,
                         NULL,
                           NULL);
  g_output_stream_write (data->dump_stream,
                         suffix,
                         strlen (suffix),
                         NULL,
                         NULL);

  g_free (prefix);
}
