/*
 * ngfd - Non-graphical feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This software, including documentation, is protected by copyright
 * controlled by Nokia Corporation. All rights are reserved.
 * Copying, including reproducing, storing, adapting or translating,
 * any or all of this material requires the prior written consent of
 * Nokia Corporation. This material also contains confidential
 * information which may not be disclosed to others without the prior
 * written consent of Nokia.
 */

#include "ngf-value.h"
#include "ngf-event.h"

#define LONG_PREFIX     "long."
#define SHORT_PREFIX    "short."

static gboolean     _event_max_timeout_cb (gpointer userdata);
static void         _stream_state_cb (NgfAudio *audio, guint stream_id, NgfStreamState state, gpointer userdata);
static const char*  _event_get_tone (NgfEvent *self);
static const char*  _event_stop_audio (NgfEvent *self);



NgfEvent*
ngf_event_new (NgfContext *context, NgfEventPrototype *proto)
{
    NgfEvent *self = NULL;

    if (context == NULL || proto == NULL)
        return NULL;

    if ((self = g_new0 (NgfEvent, 1)) == NULL)
        return NULL;

    self->context = context;
    self->proto = proto;
    self->start_timer = g_timer_new ();

    return self;
}

void
ngf_event_free (NgfEvent *self)
{
    if (self == NULL)
        return;

    if (self->start_timer) {
        g_timer_destroy (self->start_timer);
        self->start_timer = NULL;
    }

    g_free (self);
}

void
ngf_event_set_callback (NgfEvent *self, NgfEventCallback callback, gpointer userdata)
{
    self->callback = callback;
    self->userdata = userdata;
}

static gboolean
_event_max_timeout_cb (gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;
    self->max_length_timeout_id = 0;
    ngf_event_stop (self);

    if (self->callback)
        self->callback (self, NGF_EVENT_COMPLETED, self->userdata);

    return FALSE;
}

static const char*
_event_get_tone (NgfEvent *self)
{
    NgfValue *value = NULL;
    NgfEventPrototype *proto = self->proto;
    const gchar *tone = NULL;

    /* TODO returns only the long one, make one for short */

    value = g_hash_table_lookup (self->properties, "tone");
    if (value && ngf_value_get_type (value) == NGF_VALUE_STRING)
        return (const char*) ngf_value_get_string (value);

    if (proto->tone_filename)
        return (const char*) proto->tone_filename;

    if (ngf_profile_get_string (self->context->profile, proto->tone_profile, proto->tone_key, &tone))
        return (const char*) tone;

    return NULL;
}

static gint
_event_get_volume (NgfEvent *self)
{
    NgfValue *value = NULL;
    NgfEventPrototype *proto = self->proto;
    gint volume;

    value = g_hash_table_lookup (self->properties, "volume");
    if (value && ngf_value_get_type (value) == NGF_VALUE_INT)
        return ngf_value_get_int (value);

    if (proto->volume_set >= 0)
        return proto->volume_set;

    if (ngf_profile_get_integer (self->context->profile, proto->volume_profile, proto->volume_key, &volume))
        return volume;

    return -1;
}

static void
_stream_state_cb (NgfAudio *audio, guint stream_id, NgfStreamState state, gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;

    switch (state) {
        case NGF_STREAM_STARTED: {

            /* Stream started, let's notify the client that we are all up and running */

            if (self->callback)
                self->callback (self, NGF_EVENT_STARTED, self->userdata);

            break;
        }

        case NGF_STREAM_FAILED: {

            g_print ("%s: STREAM FAILED\n", __FUNCTION__);

            /* Stream failed to start, most probably reason is that the file
               does not exist. In this case, if the fallback is specified we will
               try to play it. */

            if (self->callback)
                self->callback (self, NGF_EVENT_FAILED, self->userdata);

            break;
        }

        case NGF_STREAM_TERMINATED: {

            g_print ("%s: STREAM TERMINATED\n", __FUNCTION__);

            /* Audio stream terminated. This means that the underlying audio context
               was lost (probably Pulseaudio went down). We will stop the event at
               this point (consider it completed). */

            if (self->callback)
                self->callback (self, NGF_EVENT_COMPLETED, self->userdata);

            break;
        }

        case NGF_STREAM_COMPLETED: {

            g_print ("%s: STREAM COMPLETED\n", __FUNCTION__);

            /* Stream was played and completed successfully. If the repeat flag is
               set, then we will restart the stream again. Otherwise, let's notify
               the user of the completion of the event. */

            if (self->callback)
                self->callback (self, NGF_EVENT_COMPLETED, self->userdata);

            break;
        }

        default:
            break;
    }
}

gboolean
ngf_event_start (NgfEvent *self)
{
    const char *tone = NULL;
    gint volume = 0;

    /* Check the resources and start the backends if we have the proper resources,
       profile allows us to and valid data is provided. */

    if (self->resources & NGF_RESOURCE_AUDIO) {

        /* Get the volume for the audio resource and set the volume to the stream
           restore database. */

        volume = _event_get_volume (self);
        ngf_audio_set_volume (self->context->audio, self->proto->volume_role, volume);

        if ((tone = _event_get_tone (self)) != NULL)
            self->audio_id = ngf_audio_play_stream (self->context->audio, tone, self->proto->stream_properties, _stream_state_cb, self);

    }

    /* Timeout callback for maximum length of the event. Once triggered we will
       stop the event ourselves. */

    if (self->proto->max_length > 0)
        self->max_length_timeout_id = g_timeout_add (self->proto->max_length, _event_max_timeout_cb, self);

    /* Trigger the start timer, which will be used to monitor the minimum timeout. */

    g_timer_start (self->start_timer);

    return TRUE;
}

void
ngf_event_stop (NgfEvent *self)
{
    if (self->max_length_timeout_id > 0) {
        g_source_remove (self->max_length_timeout_id);
        self->max_length_timeout_id = 0;
    }

    if (self->audio_id > 0) {
        ngf_audio_stop_stream (self->context->audio, self->audio_id);
        self->audio_id = 0;
    }
}
