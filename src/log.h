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

#ifndef _LOG_H_
#define _LOG_H_

#include "grl-xml-factory.h"

#include <gio/gio.h>

#define GRL_XML_DUMP(data, dump, length)                                \
  G_STMT_START{                                                         \
    if ((data) && (data)->dump_stream) grl_xml_dump ((data), (dump), (length)); \
  }G_STMT_END

#define GRL_XML_DEBUG_LITERAL(source, flag, literal)                    \
  G_STMT_START{                                                         \
    if (grl_xml_factory_source_is_debug ((source), (flag))) grl_xml_debug_literal((source), (literal)); \
  }G_STMT_END

#if G_HAVE_ISO_VARARGS

#define GRL_XML_DEBUG(source, flag, format, ...)                        \
  G_STMT_START{                                                         \
    if (grl_xml_factory_source_is_debug ((source), (flag))) grl_xml_debug((source), (format), __VA_ARGS__); \
  }G_STMT_END

#elif G_HAVE_GNUC_VARARGS

#define GRL_XML_DEBUG(source, flag, format, ...)                        \
  G_STMT_START{                                                         \
    if (grl_xml_factory_source_is_debug ((source), (flag))) grl_xml_debug((source), (format), ##args); \
  }G_STMT_END

#else

/* No variadic macros, use inline */
static inline void
GRL_XML_DEBUG(GrlXmlFactorySource *source,
              GrlXmlDebug flag,
              const char *format,
              ...)
{
  va_list varargs;

  if (grl_xml_factory_source_is_debug (source, flag)) {
    va_start (varargs, format);
    grl_xml_debug_valist(source, format, varargs);
    va_end (varargs);
  }
}

#endif /* G_HAVE_ISO_VARARGS */

typedef struct _LogDumpData {
  gchar *source_id;
  guint line;
  GFile *dump_file;
  GOutputStream *dump_stream;
} LogDumpData;

LogDumpData *log_dump_data_new (GrlXmlFactorySource *source,
                                guint line,
                                const gchar *dumpfile);

void log_dump_data_free (LogDumpData *data);

void grl_xml_debug_valist (GrlXmlFactorySource *source,
                           const gchar *format,
                           va_list varargs);

void grl_xml_debug (GrlXmlFactorySource *source,
                    const gchar *format,
                    ...) G_GNUC_PRINTF (2, 3);

void grl_xml_debug_literal (GrlXmlFactorySource *source,
                            const gchar *literal);

void grl_xml_dump (LogDumpData *data,
                   const gchar *dump,
                   gsize length);

#endif /* _LOG_H_*/
