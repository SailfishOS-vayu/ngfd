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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <profiled/libprofile.h>

#include "log.h"
#include "volume-controller.h"
#include "vibrator.h"
#include "sound-path.h"
#include "volume.h"
#include "profile.h"

#define KEY_VIBRATION_ENABLED   "vibrating.alert.enabled"
#define SILENT_PROFILE          "silent"
#define MEETING_PROFILE         "meeting"

#define TONE_SUFFIX             ".tone"
#define VOLUME_SUFFIX           ".volume"
#define PATTERN_SUFFIX          ".pattern"

static void
resolve_sound_path (Context    *context,
                    const char *profile,
                    const char *key,
                    const char *value)
{
    SoundPath **i = NULL;
    SoundPath  *s = NULL;

    if (context->active_profile == NULL)
        return;

    if (!g_str_has_suffix (key, TONE_SUFFIX))
        return;

    if (!profile)
        return;

    for (i = context->sounds; *i; ++i) {
        s = (SoundPath*) (*i);

        if (s->type != SOUND_PATH_TYPE_PROFILE)
            continue;

        if (!s->key || !key || !g_str_equal (s->key, key))
            continue;

        if ((!s->profile && g_str_equal (context->active_profile, profile)) || (s->profile && g_str_equal (s->profile, profile))) {
            g_free (s->filename);
            s->filename = g_strdup (value);
            break;
        }
    }
}

static void
resolve_volume (Context    *context,
                const char *profile,
                const char *key,
                const char *value)
{
    Volume **i = NULL;
    Volume  *s = NULL;

    if (context->active_profile == NULL)
        return;

    if (!g_str_has_suffix (key, VOLUME_SUFFIX))
        return;

    if (!profile)
        return;

    for (i = context->volumes; *i; ++i) {
        s = (Volume*) (*i);

        if (s->type != VOLUME_TYPE_PROFILE)
            continue;

        if (!s->key || !key || !g_str_equal (s->key, key))
            continue;

        if ((!s->profile && g_str_equal (context->active_profile, profile)) || (s->profile && g_str_equal (s->profile, profile))) {
            s->level = profile_parse_int (value);
            volume_controller_update (context, s);
            break;
        }
    }
}

static void
resolve_vibration (Context    *context,
                   const char *profile,
                   const char *key,
                   const char *value)
{
    VibrationPattern **i = NULL;
    VibrationPattern  *p = NULL;

    if (context->active_profile == NULL)
        return;

    if (!g_str_has_suffix (key, PATTERN_SUFFIX))
        return;

    if (!profile)
        return;

    for (i = context->patterns; *i; ++i) {
        p = (VibrationPattern*) (*i);

        if (p->type != VIBRATION_PATTERN_TYPE_PROFILE)
            continue;

        if (!p->key || !key || !g_str_equal (p->key, key))
            continue;

        if ((!p->profile && g_str_equal (context->active_profile, profile)) || (p->profile && g_str_equal (p->profile, profile))) {
            g_free (p->filename);
            g_free (p->data);

            p->filename = g_strdup (value);
            if ((p->data = vibrator_load (p->filename)) == NULL)
                NGF_LOG_WARNING ("%s >> failed to load vibrator pattern data: %s", __FUNCTION__, p->filename);

            break;
        }
    }
}

static void
resolve_profile (Context    *context,
                 const char *profile)
{
    context->silent_mode  = FALSE;
    context->meeting_mode = FALSE;

    if (!profile)
        return;

    if (g_str_equal (profile, SILENT_PROFILE))
        context->silent_mode = TRUE;

    else if (g_str_equal (profile, MEETING_PROFILE))
        context->meeting_mode = TRUE;

    g_free (context->active_profile);
    context->active_profile = g_strdup (profile);
}

static void
value_changed_cb (const char *profile,
				  const char *key,
				  const char *value,
				  const char *type,
				  void *userdata)
{
    Context *context = (Context*) userdata;

    if (!profile || !key)
        return;

    if (context->active_profile && g_str_equal (context->active_profile, profile)) {
        if (g_str_equal (key, KEY_VIBRATION_ENABLED)) {
            context->vibration_enabled = profile_parse_bool (value);
            return;
        }
    }

    resolve_sound_path (context, profile, key, value);
    resolve_volume     (context, profile, key, value);
    resolve_vibration  (context, profile, key, value);
}

static void
profile_changed_cb (const char *profile,
                    void *userdata)
{
    Context *context = (Context*) userdata;
    g_print ("profile changed %s -> %s\n", context->active_profile, profile);
    resolve_profile (context, profile);
}

int
profile_create (Context *context)
{
    profile_connection_disable_autoconnect ();
    profile_reconnect (context);

    profile_track_add_active_cb  (value_changed_cb, context, NULL);
    profile_track_add_change_cb  (value_changed_cb, context, NULL);
    profile_track_add_profile_cb (profile_changed_cb, context, NULL);

    profile_tracker_init ();

    return TRUE;
}

int
profile_resolve (Context *context)
{
    SoundPath        **i     = NULL;
    Volume           **j     = NULL;
    VibrationPattern **p     = NULL;

    context->active_profile    = profile_get_profile ();
    context->vibration_enabled = profile_get_value_as_bool (NULL, KEY_VIBRATION_ENABLED);

    if (!context->active_profile)
        return FALSE;

    if (g_str_equal (context->active_profile, SILENT_PROFILE))
        context->silent_mode = TRUE;
    else if (g_str_equal (context->active_profile, MEETING_PROFILE))
        context->meeting_mode = TRUE;

    if (context->sounds) {
        for (i = context->sounds; *i; ++i) {
            SoundPath *s = (SoundPath*) (*i);

            if (s->type != SOUND_PATH_TYPE_PROFILE)
                continue;

            g_free (s->filename);
            s->filename = profile_get_value (s->profile, s->key);
        }
    }

    if (context->volumes) {
        for (j = context->volumes; *j; ++j) {
            Volume *v = (Volume*) (*j);

            if (v->type != VOLUME_TYPE_PROFILE)
                continue;

            v->level = profile_get_value_as_int (v->profile, v->key);
        }
    }

    if (context->patterns) {
        for (p = context->patterns; *p; ++p) {
            VibrationPattern *pattern = (VibrationPattern*) (*p);

            if (pattern->type != VIBRATION_PATTERN_TYPE_PROFILE)
                continue;

            g_free (pattern->filename);
            g_free (pattern->data);

            pattern->filename = profile_get_value (pattern->profile, pattern->key);
            pattern->pattern  = 0;

            if ((pattern->data = vibrator_load (pattern->filename)) == NULL)
                NGF_LOG_WARNING ("%s >> failed to load vibrator pattern data: %s", __FUNCTION__, pattern->filename);
        }
    }

    return TRUE;
}

int
profile_reconnect (Context *context)
{
    if (!context->session_bus) {
        NGF_LOG_DEBUG ("%s >> no session bus available.", __FUNCTION__);
        return FALSE;
    }

    profile_connection_set (context->session_bus);

    /* resolve again and update volumes */
    profile_resolve              (context);
    volume_controller_update_all (context);

    return FALSE;
}

void
profile_destroy (Context *context)
{
    (void) context;
    profile_tracker_quit ();
}


