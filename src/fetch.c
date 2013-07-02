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

#include "fetch.h"

#include <string.h>

typedef struct _NetProcessData {
  FetchData *fetch_data;
  GrlXmlFactorySource *source;
  GrlXmlDebug debug;
  GrlNetWc *wc;
  ExpandData *expand_data;
  GCancellable *cancellable;
  DataFetchedCb callback;
  gpointer user_data;
} NetProcessData;

typedef struct  _ExpressionProcessData {
  NetProcessData *net_data;
  GetRawCb get_raw_callback;
  DataRef *get_raw_data;
} ExpressionProcessData;

typedef struct _RegexpProcessData {
  ExpressionProcessData common;
  FetchData *data;
  GList *current_subregexp;
} RegexpProcessData;

typedef struct _ReplaceProcessData {
  ExpressionProcessData common;
  ReplaceData *replace;
} ReplaceProcessData;

static void fetch_subregexp (RegexpProcessData *data);

static void fetch_regexp (GrlXmlFactorySource *source,
                          GrlXmlDebug debug_flag,
                          GrlNetWc *wc,
                          FetchData *fetch_data,
                          ExpandData *expand_data,
                          GCancellable *cancellable,
                          GetRawCb get_raw_callback,
                          DataRef *get_raw_data,
                          DataFetchedCb send_callback,
                          gpointer user_data);

inline static NetProcessData *
net_process_data_new (void)
{
  return g_slice_new0 (NetProcessData);
}

static void
net_process_data_free (NetProcessData *data)
{
  if (data->source) {
    g_object_unref (data->source);
  }
  if (data->wc) {
    g_object_unref (data->wc);
  }
  if (data->expand_data) {
    expand_data_unref (data->expand_data);
  }
  if (data->cancellable) {
    g_object_unref (data->cancellable);
  }

  g_slice_free (NetProcessData, data);
}

static void
expression_process_data_free (ExpressionProcessData *data) {
  net_process_data_free (data->net_data);
  if (data->get_raw_data) {
    dataref_unref (data->get_raw_data);
  }
}

static ReplaceProcessData *
replace_process_data_new (void)
{
  ReplaceProcessData *data;
  data = g_slice_new0 (ReplaceProcessData);
  data->common.net_data = net_process_data_new ();

  return data;
}

static void
replace_process_data_free (ReplaceProcessData *data)
{
  expression_process_data_free (&(data->common));
  g_slice_free (ReplaceProcessData, data);
}

static RegexpProcessData *
regexp_process_data_new (void)
{
  RegexpProcessData *data;

  data = g_slice_new0 (RegexpProcessData);
  data->common.net_data = net_process_data_new ();

  return data;
}

static void
regexp_process_data_free (RegexpProcessData *data)
{
  expression_process_data_free (&(data->common));
  g_slice_free (RegexpProcessData, data);
}

static void
fetch_replace_input_obtained (const gchar *input,
                              ReplaceProcessData *data,
                              const GError *error)
{
  GRegex *regex;
  gchar *expanded_expression;
  gchar *expanded_replacement;
  gchar *output;

  if (error || !input) {
    data->common.net_data->callback (NULL, data->common.net_data->user_data, error);
    replace_process_data_free (data);
    return;
  }

  if (!data->replace->expression) {
    data->common.net_data->callback (input, data->common.net_data->user_data, NULL);
    replace_process_data_free (data);
    return;
  }

  expanded_expression = expandable_string_get_value (data->replace->expression,
                                                     data->common.net_data->expand_data);

  if ((regex = g_regex_new (expanded_expression, 0, 0, NULL)) == NULL) {
    data->common.net_data->callback (NULL, data->common.net_data->user_data, NULL);
    expandable_string_free_value (data->replace->expression, expanded_expression);
    replace_process_data_free (data);
    return;
  }
  expandable_string_free_value (data->replace->expression, expanded_expression);

  if (data->replace->replacement) {
    expanded_replacement = expandable_string_get_value (data->replace->replacement,
                                                        data->common.net_data->expand_data);
  } else {
    expanded_replacement = "";
  }

  output = g_regex_replace (regex, input, -1, 0, expanded_replacement, 0, NULL);

  g_regex_unref (regex);

  if (data->replace->replacement) {
    expandable_string_free_value (data->replace->replacement, expanded_replacement);
  }

  GRL_XML_DUMP (data->common.net_data->fetch_data->dump, output, strlen (output));

  data->common.net_data->callback (output, data->common.net_data->user_data, NULL);

  g_free (output);
  replace_process_data_free (data);
}

static void
fetch_regexp_input_obtained (const gchar *input,
                             RegexpProcessData *data,
                             const GError *error)
{
  GMatchInfo *match_info;
  GRegex *regex;
  GString *result;
  gboolean repeat;
  gchar *expanded_expression;
  gchar *expanded_output;

  if (error || !input) {
    data->common.net_data->callback (NULL, data->common.net_data->user_data, error);
    regexp_process_data_free (data);
    return;
  }

  //if (data->data.regexp->expression->expression) {
  if (data->data->data.regexp->expression->expression) {
    expanded_expression = expandable_string_get_value (data->data->data.regexp->expression->expression,
                                                       data->common.net_data->expand_data);

    if ((regex = g_regex_new (expanded_expression, 0, 0, NULL)) == NULL) {
      data->common.net_data->callback (NULL, data->common.net_data->user_data, NULL);
      expandable_string_free_value (data->data->data.regexp->expression->expression, expanded_expression);
      regexp_process_data_free (data);
      return;
    }
    expandable_string_free_value (data->data->data.regexp->expression->expression, expanded_expression);
    repeat = data->data->data.regexp->expression->repeat;
  } else {
    expanded_expression = "(?ms)(.*)";
    regex = g_regex_new (expanded_expression, 0, 0, NULL);
    repeat = FALSE;
  }

  if (data->data->data.regexp->output) {
    expanded_output = expandable_string_get_value (data->data->data.regexp->output,
                                                   data->common.net_data->expand_data);
  } else {
    expanded_output = "\\1";
  }

  g_regex_match_full (regex, input, -1, 0, 0, &match_info, NULL);

  result = g_string_new ("");
  if (repeat) {
    while (g_match_info_matches (match_info)) {
      g_string_append (result, g_match_info_expand_references (match_info, expanded_output, NULL));
      g_match_info_next (match_info, NULL);
    }
  } else if (g_match_info_matches (match_info)) {
    g_string_append (result, g_match_info_expand_references (match_info, expanded_output, NULL));
  }

  g_match_info_free (match_info);
  g_regex_unref (regex);

  if (data->data->data.regexp->output) {
    expandable_string_free_value (data->data->data.regexp->output, expanded_output);
  }

  GRL_XML_DUMP (data->data->dump, result->str, strlen(result->str));

  data->common.net_data->callback (result->str[0] != '\0'? result->str: NULL,
                                   data->common.net_data->user_data,
                                   NULL);

  g_string_free (result, TRUE);
  regexp_process_data_free (data);
}

static void
fetch_regexp_subregexp_obtained (const gchar *subregexp,
                                 RegexpProcessData *data,
                                 const GError *error)
{
  FetchData *current_subregexp;

  if (subregexp) {
    current_subregexp = (FetchData *) data->current_subregexp->data;
    expand_data_add_buffer (data->common.net_data->expand_data, current_subregexp->data.regexp->output_id, subregexp);
  }
  data->current_subregexp = g_list_next (data->current_subregexp);
  fetch_subregexp (data);
}

/* Callback used when @wc has fetched the url content */
static void
fetch_data_url_content_obtained (GrlNetWc *wc,
                                 GAsyncResult *res,
                                 NetProcessData *data)
{
  GError *error = NULL;
  GError *net_error = NULL;
  gchar *content = NULL;
  gsize size;

  if (g_cancellable_is_cancelled (data->cancellable)) {
      error = g_error_new (GRL_CORE_ERROR,
                           GRL_CORE_ERROR_OPERATION_CANCELLED,
                           "Operation has been cancelled");
  }

  if (!error &&
      !grl_net_wc_request_finish (wc, res, &content, &size, &net_error)) {
    error = g_error_new (GRL_CORE_ERROR,
                         0,
                         "Unable to read source: %s", net_error->message);
    g_error_free (net_error);
  }

  GRL_XML_DUMP (data->fetch_data->dump, content, size);

  data->callback (content, data->user_data, error);
  if (error) {
    g_error_free (error);
  }

  net_process_data_free (data);
}

static void
fetch_rest_fetched (RestProxyCall *call,
                    const GError *rest_error,
                    GObject *weak_object,
                    NetProcessData *data)
{
  GError *error = NULL;
  const gchar *content = NULL;

  if (rest_error) {
    data->callback (NULL, data->user_data, rest_error);
    g_slice_free (NetProcessData, data);
    return;
  }

  if (g_cancellable_is_cancelled (data->cancellable)) {
    error = g_error_new (GRL_CORE_ERROR,
                         GRL_CORE_ERROR_OPERATION_CANCELLED,
                         "Operation has been cancelled");
  }

  if (!error) {
    content = rest_proxy_call_get_payload (call);
  }

  GRL_XML_DUMP (data->fetch_data->dump,
                content,
                (gsize) rest_proxy_call_get_payload_length (call));

  data->callback (content, data->user_data, error);
  if (error) {
    g_error_free (error);
  }

  g_slice_free (NetProcessData, data);
}

static void
fetch_rest (GrlXmlFactorySource *source,
            GrlXmlDebug debug_flag,
            FetchData *fetch_data,
            ExpandData *expand_data,
            GCancellable *cancellable,
            GetRawCb get_raw_callback,
            DataRef *get_raw_data,
            DataFetchedCb send_callback,
            gpointer user_data)
{
  GError *call_error = NULL;
  GError *error;
  GList *parameters;
  NetProcessData *data;
  RestParameter *param;
  RestProxyCall *call;
  gchar *use_function;
  gchar *use_value;

  call = rest_proxy_new_call (fetch_data->data.rest->proxy);

  if (fetch_data->data.rest->function) {
    use_function = get_raw_callback (source, fetch_data->data.rest->function, get_raw_data);
    if (!use_function) {
      send_callback (NULL, user_data, NULL);
      g_object_unref (call);
      return;
    }

    rest_proxy_call_set_function (call, use_function);
    expandable_string_free_value (fetch_data->data.rest->function, use_function);
  }

  /* Expand each of the parameters */
  for (parameters = fetch_data->data.rest->parameters;
       parameters;
       parameters = g_list_next (parameters)) {
    param = (RestParameter *) parameters->data;
    use_value = get_raw_callback (source, param->value, get_raw_data);
    if (!use_value) {
      send_callback (NULL, user_data, NULL);
      g_object_unref (call);
      return;
    }

    rest_proxy_call_add_param (call, param->name, use_value);
    expandable_string_free_value (param->value, use_value);
  }

  data = net_process_data_new ();
  data->fetch_data = fetch_data;
  data->source = g_object_ref (source);
  data->debug = debug_flag;
  data->callback = send_callback;
  data->cancellable = cancellable;
  data->user_data = user_data;

  rest_proxy_call_set_method (call, fetch_data->data.rest->method);

  if (!rest_proxy_call_async (call,
                              (RestProxyCallAsyncCallback) fetch_rest_fetched,
                              NULL,
                              data,
                              &call_error)) {
    g_slice_free (NetProcessData, data);
    error = g_error_new (GRL_CORE_ERROR, 0, "Cannot invoke RESTful: %s", call_error->message);
    send_callback (NULL, user_data, error);
    g_error_free (call_error);
    g_error_free (error);
  }

  g_object_unref (call);
}

static void
fetch_subregexp (RegexpProcessData *data)
{
  const gchar *input;

  if (data->current_subregexp) {
    fetch_regexp (data->common.net_data->source,
                  data->common.net_data->debug,
                  data->common.net_data->wc,
                  data->current_subregexp->data,
                  data->common.net_data->expand_data,
                  data->common.net_data->cancellable,
                  data->common.get_raw_callback,
                  data->common.get_raw_data,
                  (DataFetchedCb) fetch_regexp_subregexp_obtained,
                  data);
  } else {
    if (data->data->data.regexp->input->use_ref) {
      input = expand_data_get_buffer (data->common.net_data->expand_data,
                                      data->data->data.regexp->input->data.buffer_id);
      fetch_regexp_input_obtained (input, data, NULL);
    } else {
      fetch_data_get (data->common.net_data->source,
                      data->common.net_data->debug,
                      data->common.net_data->wc,
                      data->data->data.regexp->input->data.input,
                      data->common.net_data->expand_data,
                      data->common.net_data->cancellable,
                      data->common.get_raw_callback,
                      data->common.get_raw_data,
                      (DataFetchedCb) fetch_regexp_input_obtained,
                      data);
    }
  }
}

static void
fetch_regexp (GrlXmlFactorySource *source,
              GrlXmlDebug debug_flag,
              GrlNetWc *wc,
              FetchData *fetch_data,
              ExpandData *expand_data,
              GCancellable *cancellable,
              GetRawCb get_raw_callback,
              DataRef *get_raw_data,
              DataFetchedCb send_callback,
              gpointer user_data)
{
  RegexpProcessData *rp_data;

  rp_data = regexp_process_data_new ();
  rp_data->common.net_data->source = g_object_ref (source);
  rp_data->common.net_data->debug = debug_flag;
  rp_data->common.net_data->wc = g_object_ref (wc);
  rp_data->common.net_data->expand_data = expand_data_ref (expand_data);
  rp_data->common.net_data->cancellable = g_object_ref (cancellable);
  rp_data->common.net_data->callback = send_callback;
  rp_data->common.net_data->user_data = user_data;
  rp_data->common.get_raw_callback = get_raw_callback;
  rp_data->common.get_raw_data = dataref_ref (get_raw_data);
  rp_data->data = fetch_data;
  if (fetch_data->data.regexp->subregexp) {
    rp_data->current_subregexp = fetch_data->data.regexp->subregexp;
  }
  fetch_subregexp (rp_data);
}

static void
fetch_data_url_obtained (const gchar *url,
                         NetProcessData *data,
                         const GError *error)
{
  if (error || !url || *url == '\0') {
    data->callback (NULL, data->user_data, error);
    net_process_data_free (data);
    return;
  }

  GRL_XML_DEBUG (data->source, data->debug, "Read '%s' URL", url);
  grl_net_wc_request_async (data->wc, url, data->cancellable,
                            (GAsyncReadyCallback) fetch_data_url_content_obtained,
                            data);
}

RegExpInput *
reg_exp_input_new ()
{
  RegExpInput *data;

  data = g_slice_new0 (RegExpInput);
  data->use_ref = TRUE;

  return data;
}

void
reg_exp_input_free (RegExpInput *input)
{
  if (input->use_ref) {
    g_free (input->data.buffer_id);
  } else {
    fetch_data_free (input->data.input);
  }

  g_slice_free (RegExpInput, input);
}

RegExpExpression *
reg_exp_expression_new ()
{
  return g_slice_new0 (RegExpExpression);
}

void
reg_exp_expression_free (RegExpExpression *expression)
{
  expandable_string_free (expression->expression);
  g_slice_free (RegExpExpression, expression);
}

RegExpData *
reg_exp_data_new ()
{
  RegExpData *data;

  data = g_slice_new0 (RegExpData);
  data->input = reg_exp_input_new ();
  data->expression = reg_exp_expression_new ();

  return data;
}

void
reg_exp_data_free (RegExpData *data)
{
  g_list_free_full (data->subregexp, (GDestroyNotify) reg_exp_data_free);
  reg_exp_input_free (data->input);
  expandable_string_free (data->output);
  g_free (data->output_id);
  reg_exp_expression_free (data->expression);
  g_slice_free (RegExpData, data);
}

ReplaceData *
replace_data_new ()
{
  return g_slice_new (ReplaceData);
}

void
replace_data_free (ReplaceData *data)
{
  if (data->input) {
    fetch_data_free (data->input);
  }
  expandable_string_free (data->replacement);
  expandable_string_free (data->expression);
  g_slice_free (ReplaceData, data);
}

RestParameter *
rest_parameter_new (gchar *name,
                    ExpandableString *value)
{
  RestParameter *parameter;

  parameter = g_slice_new (RestParameter);
  parameter->name = name;
  parameter->value = value;

  return parameter;
}

void
rest_parameter_free (RestParameter *parameter)
{
  g_free (parameter->name);
  expandable_string_free (parameter->value);
  g_slice_free (RestParameter, parameter);
}

RestData *
rest_data_new ()
{
  RestData *data;

  data = g_slice_new0 (RestData);

  return data;
}

void
rest_data_free (RestData *data)
{
  if (data->proxy) {
    g_object_unref (data->proxy);
  }
  g_free (data->method);
  expandable_string_free  (data->function);

  g_list_free_full (data->parameters, (GDestroyNotify) rest_parameter_free);

  g_slice_free (RestData, data);
}

FetchData *
fetch_data_new ()
{
  return g_slice_new0 (FetchData);
}

void
fetch_data_free (FetchData *data)
{
  log_dump_data_free (data->dump);

  switch (data->type) {
  case FETCH_RAW:
    expandable_string_free (data->data.raw);
    break;
  case FETCH_URL:
    fetch_data_free (data->data.url);
    break;
  case FETCH_REST:
    rest_data_free (data->data.rest);
    break;
  case FETCH_REPLACE:
    replace_data_free (data->data.replace);
    break;
  case FETCH_REGEXP:
    reg_exp_data_free (data->data.regexp);
    break;
  }

  g_slice_free (FetchData, data);
}

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
                gpointer user_data)
{
  NetProcessData *net_data;
  ReplaceProcessData *replace_data;
  gchar *use_raw;

  if (fetch_data->type == FETCH_RAW) {
    use_raw = get_raw_callback (source, fetch_data->data.raw, get_raw_data);
    GRL_XML_DEBUG (source, debug_flag, "Use '%s'", use_raw);
    send_callback (use_raw, user_data, NULL);
    expandable_string_free_value (fetch_data->data.raw, use_raw);
  }

  if (fetch_data->type == FETCH_URL) {
    net_data = net_process_data_new ();
    net_data->fetch_data = fetch_data;
    net_data->source = g_object_ref (source);
    net_data->debug = debug_flag;
    net_data->wc = g_object_ref (wc);
    net_data->expand_data = expand_data_ref (expand_data);
    net_data->cancellable = g_object_ref (cancellable);
    net_data->callback = send_callback;
    net_data->user_data = user_data;
    fetch_data_get (source,
                    debug_flag,
                    wc,
                    fetch_data->data.url,
                    expand_data,
                    cancellable,
                    get_raw_callback,
                    get_raw_data,
                    (DataFetchedCb) fetch_data_url_obtained,
                    net_data);
  }

  if (fetch_data->type == FETCH_REST) {
    fetch_rest (source,
                debug_flag,
                fetch_data,
                expand_data,
                cancellable,
                get_raw_callback,
                get_raw_data,
                send_callback,
                user_data);
  }

  if (fetch_data->type == FETCH_REPLACE) {
    replace_data = replace_process_data_new ();
    replace_data->common.net_data->fetch_data = fetch_data;
    replace_data->common.net_data->source = g_object_ref (source);
    replace_data->common.net_data->debug = debug_flag;
    replace_data->common.net_data->wc = g_object_ref (wc);
    replace_data->common.net_data->expand_data = expand_data_ref (expand_data);
    replace_data->common.net_data->cancellable = g_object_ref (cancellable);
    replace_data->common.net_data->callback = send_callback;
    replace_data->common.net_data->user_data = user_data;
    replace_data->common.get_raw_callback = get_raw_callback;
    replace_data->common.get_raw_data = dataref_ref (get_raw_data);
    replace_data->replace = fetch_data->data.replace;
    fetch_data_get (source,
                    debug_flag,
                    wc,
                    fetch_data->data.replace->input,
                    expand_data,
                    cancellable,
                    get_raw_callback,
                    get_raw_data,
                    (DataFetchedCb) fetch_replace_input_obtained,
                    replace_data);
  }

  if (fetch_data->type == FETCH_REGEXP) {
    fetch_regexp (source,
                  debug_flag,
                  wc,
                  fetch_data,
                  expand_data,
                  cancellable,
                  get_raw_callback,
                  get_raw_data,
                  send_callback,
                  user_data);
  }
}
