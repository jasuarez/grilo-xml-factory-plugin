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

#ifndef _FETCH_H_
#define _FETCH_H_

#include "dataref.h"
#include "expandable-string.h"
#include "log.h"

#include <glib.h>
#include <net/grl-net.h>
#include <rest/rest-proxy.h>

enum {
  FETCH_RAW,
  FETCH_URL,
  FETCH_REST,
  FETCH_REPLACE,
  FETCH_REGEXP,
};

typedef void (*DataFetchedCb) (const gchar *content,
                               gpointer user_data,
                               const GError *error);

typedef gchar *(*GetRawCb) (GrlXmlFactorySource *source,
                            ExpandableString *raw,
                            DataRef *data);

typedef struct _FetchData FetchData;

typedef struct _RegExpExpression {
  gboolean repeat;
  ExpandableString *expression;
} RegExpExpression;

typedef struct _RegExpInput {
  gboolean decode;
  gboolean use_ref;
  union {
    gchar *buffer_id;
    FetchData *input;
  } data;
} RegExpInput;

typedef struct _RegExpData {
  GList *subregexp;
  RegExpInput *input;
  ExpandableString *output;
  gchar *output_id;
  RegExpExpression *expression;
} RegExpData;

typedef struct _ReplaceData {
  FetchData *input;
  ExpandableString *replacement;
  ExpandableString *expression;
} ReplaceData;

typedef struct _RestParameter {
  gchar *name;
  ExpandableString *value;
} RestParameter;

typedef struct _RestData {
  RestProxy *proxy;
  gchar *endpoint;
  gchar *method;
  ExpandableString *referer;
  ExpandableString *function;
  GList *parameters;
} RestData;

struct _FetchData {
  LogDumpData *dump;
  gint type;
  union {
    ExpandableString *raw;
    FetchData *url;
    RestData *rest;
    ReplaceData *replace;
    RegExpData *regexp;
  } data;
};

RegExpInput *reg_exp_input_new (void);

void reg_exp_input_free (RegExpInput *input);

RegExpExpression *reg_exp_expression_new (void);

void reg_exp_expression_free (RegExpExpression *expression);

RegExpData *reg_exp_data_new (void);

void reg_exp_data_free (RegExpData *data);

ReplaceData *replace_data_new (void);

void replace_data_free (ReplaceData *data);

RestParameter *rest_parameter_new (gchar *name,
                                   ExpandableString *value);

void rest_parameter_free (RestParameter *parameter);

RestData *rest_data_new (void);

void rest_data_free (RestData *data);

FetchData *fetch_data_new (void);

void fetch_data_free (FetchData *data);

void
fetch_data_get (GrlXmlFactorySource *source,
                GrlXmlDebug debug_flag,
                GrlNetWc *wc,
                FetchData *fetch_data,
                ExpandData *expand_data,
                GCancellable *cancellable,
                GetRawCb get_raw_callback,
                DataRef *get_raw_data,
                DataFetchedCb send_callback,
                gpointer user_data);

#endif /* _FETCH_H_*/
