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

#ifndef _GRL_XML_FACTORY_SOURCE_H_
#define _GRL_XML_FACTORY_SOURCE_H_

#include <grilo.h>

GRL_LOG_DOMAIN_EXTERN(xml_factory_log_domain);

#define GRL_XML_FACTORY_SOURCE_TYPE             \
  (grl_xml_factory_source_get_type ())

#define GRL_XML_FACTORY_SOURCE(obj)                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                       \
                               GRL_XML_FACTORY_SOURCE_TYPE, \
                               GrlXmlFactorySource))

#define GRL_IS_XML_FACTORY_SOURCE(obj)                         \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                          \
                               GRL_XML_FACTORY_SOURCE_TYPE))

#define GRL_XML_FACTORY_SOURCE_CLASS(klass)              \
  (G_TYPE_CHECK_CLASS_CAST((klass),                      \
                           GRL_XML_FACTORY_SOURCE_TYPE,  \
                           GrlXmlFactorySourceClass))

#define GRL_IS_XML_FACTORY_SOURCE_CLASS(klass)           \
  (G_TYPE_CHECK_CLASS_TYPE((klass),                      \
                           GRL_XML_FACTORY_SOURCE_TYPE))

#define GRL_XML_FACTORY_SOURCE_GET_CLASS(obj)               \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                        \
                              GRL_XML_FACTORY_SOURCE_TYPE,  \
                              GrlXmlFactorySourceClass))

typedef enum {
  GRL_XML_DEBUG_NONE      = 0,
  GRL_XML_DEBUG_OPERATION = 1,
  GRL_XML_DEBUG_PROVIDE   = 1 << 1
} GrlXmlDebug;

typedef struct _GrlXmlFactorySource        GrlXmlFactorySource;
typedef struct _GrlXmlFactorySourcePrivate GrlXmlFactorySourcePrivate;

struct _GrlXmlFactorySource {

  GrlSource parent;

  /*< private >*/
  GrlXmlFactorySourcePrivate *priv;
};

typedef struct _GrlXmlFactorySourceClass GrlXmlFactorySourceClass;

struct _GrlXmlFactorySourceClass {

  GrlSourceClass parent_class;

};

GType grl_xml_factory_source_get_type (void);

gboolean grl_xml_factory_source_is_debug (GrlXmlFactorySource *source,
                                          GrlXmlDebug flag);

gchar *grl_xml_factory_source_run_script (GrlXmlFactorySource *source,
                                          const gchar *script,
                                          GError **error);

#endif /* _GRL_XML_FACTORY_SOURCE_H_ */
