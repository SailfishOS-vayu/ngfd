/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <ngf/plugin.h>
#include <ngf/core.h>
#include <string.h>

#define LOG_CAT              "transform: "
#define TRANSFORM_KEY_PREFIX "transform."

N_PLUGIN_NAME        ("transform")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_AUTHOR      ("Harri Mahonen <ext-harri.mahonen@nokia.com>")
N_PLUGIN_DESCRIPTION ("Transform request properties")

static gboolean    transform_allow_all = FALSE;
static GList      *transform_allowed_keys = NULL;
static GHashTable *transform_key_map = NULL;

static void
new_request_cb (NHook *hook, void *data, void *userdata)
{
    (void) hook;
    (void) data;
    (void) userdata;

    NProplist  *props     = NULL;
    NProplist  *new_props = NULL;
    GList      *iter      = NULL;
    const char *key       = NULL;
    const char *map_key   = NULL;
    NValue     *value     = NULL;

    NCoreHookTransformPropertiesData *transform = (NCoreHookTransformPropertiesData*) data;

    N_DEBUG (LOG_CAT "transforming request keys for request '%s'",
        n_request_get_name (transform->request));

    if (transform_allow_all) {
        N_DEBUG (LOG_CAT "all keys are allowed, just a passthrough.");
        return;
    }

    new_props = n_proplist_new ();
    props     = (NProplist*) n_request_get_properties (transform->request);

    for (iter = g_list_first (transform_allowed_keys); iter; iter = g_list_next (iter)) {
        key     = (const char*) iter->data;
        value   = n_proplist_get (props, key);
        map_key = g_hash_table_lookup (transform_key_map, key);

        if (map_key) {
            N_DEBUG (LOG_CAT "+ transforming key '%s' to '%s'", key, map_key);
            n_proplist_set (new_props, map_key, n_value_copy (value));
        }
        else {
            N_DEBUG (LOG_CAT "+ allowing value '%s'", key);
            n_proplist_set (new_props, key, n_value_copy (value));
        }
    }

    n_request_set_properties (transform->request, new_props);
}

static int
parse_allowed_keys (NProplist *params)
{
    const char  *str   = NULL;
    gchar      **split = NULL;
    gchar      **item  = NULL;

    str = n_proplist_get_string (params, "allow");
    if (!str) {
        N_WARNING (LOG_CAT "no allow key specified.");
        return FALSE;
    }

    if (g_str_equal (str, "*")) {
        N_DEBUG (LOG_CAT "allowing all incoming keys");
        transform_allow_all = TRUE;
        return TRUE;
    }

    split = g_strsplit (str, " ", -1);
    for (item = split; *item; ++item) {
        N_DEBUG (LOG_CAT "allowed key '%s'", *item);
        transform_allowed_keys = g_list_append (transform_allowed_keys,
            g_strdup (*item));
    }
    g_strfreev (split);

    return TRUE;
}

static void
parse_transform_key_cb (const char *key, const NValue *value,
                        gpointer userdata)
{
    (void) userdata;

    const char *new_key = NULL;
    const char *str     = NULL;

    if (!g_str_has_prefix (key, TRANSFORM_KEY_PREFIX))
        return;

    new_key = key + strlen (TRANSFORM_KEY_PREFIX);
    if (*new_key == '\0')
        return;

    str = n_value_get_string ((NValue*) value);
    g_hash_table_replace (transform_key_map, g_strdup (new_key),
        g_strdup (str));

    N_DEBUG (LOG_CAT "will transform key '%s' to '%s'", new_key, str);
}

static int
parse_transform_map (NProplist *params)
{
    n_proplist_foreach (params, parse_transform_key_cb, NULL);
    return TRUE;
}

N_PLUGIN_LOAD (plugin)
{
    NCore     *core   = NULL;
    NProplist *params = NULL;

    core   = n_plugin_get_core (plugin);
    params = (NProplist*) n_plugin_get_params (plugin);

    transform_key_map = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, g_free);

    /* parse the allowed keys */

    if (!parse_allowed_keys (params))
        return FALSE;

    /* parse the transform map */

    if (!parse_transform_map (params))
        return FALSE;

    /* connect to the new request hook. */

    (void) n_core_connect (core, N_CORE_HOOK_NEW_REQUEST,
        0, new_request_cb, core);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;

    if (transform_allowed_keys) {
        g_list_foreach (transform_allowed_keys, (GFunc) g_free, NULL);
        g_list_free (transform_allowed_keys);
        transform_allowed_keys = NULL;
    }

    g_hash_table_destroy (transform_key_map);
    transform_key_map = NULL;
}
