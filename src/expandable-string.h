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

#ifndef _EXPANDABLE_STRING_H_
#define _EXPANDABLE_STRING_H_

#include "grl-xml-factory.h"

typedef struct _ExpandableString ExpandableString;

typedef struct _ExpandData ExpandData;


ExpandData *expand_data_new (GrlXmlFactorySource *source,
                             GrlMedia *media,
                             const gchar *search_text,
                             GrlOperationOptions *options);

ExpandData *expand_data_ref (ExpandData *data);

void expand_data_unref (ExpandData *data);

void expand_data_add_buffer (ExpandData *data,
                             const gchar *buffer_id,
                             const gchar *buffer_content);

const gchar *expand_data_get_buffer (ExpandData *data,
                                     const gchar *buffer_id);

ExpandableString *expandable_string_new (const gchar *init,
                                         GrlConfig *config,
                                         GList *located_strings);

void expandable_string_free (ExpandableString *exp_str);

gchar *expandable_string_get_value (ExpandableString *exp_str,
                                    ExpandData *data);

void expandable_string_free_value (ExpandableString *exp_str,
                                   gchar *value);

gchar *expand_html_entities (const gchar *str);

#endif /* _EXPANDABLE_STRING_H_ */
