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

#include "ngf-log.h"
#include "ngf-value.h"
#include "ngf-event.h"
#include "ngf-tone-mapper.h"

static gboolean     _event_max_timeout_cb (gpointer userdata);
static void         _stream_state_cb (NgfAudio *audio, guint stream_id, NgfStreamState state, gpointer userdata);
static const char*  _get_mapped_tone (NgfToneMapper *mapper, const char *tone);
static const char*  _event_get_tone (NgfEvent *self);
static const char*  _event_get_fallback (NgfEvent *self);
static const char*  _event_get_vibra (NgfEvent *self);
static const char*  _event_get_led (NgfEvent *self);
static gboolean     _event_is_vibra_enabled (NgfEvent *self);
static gboolean     _volume_control_cb (guint id, guint step_time, guint step_value, gpointer userdata);
static gboolean     _setup_audio (NgfEvent *self);
static gboolean     _setup_vibrator (NgfEvent *self);
static gboolean     _setup_led (NgfEvent *self);
static gboolean     _setup_backlight (NgfEvent *self);
static void         _shutdown_audio (NgfEvent *self);
static void         _shutdown_vibrator (NgfEvent *self);
static void         _shutdown_led (NgfEvent *self);
static void         _shutdown_backlight (NgfEvent *self);



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

    if (self->properties) {
        g_hash_table_destroy (self->properties);
        self->properties = NULL;
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

/**
 * Get the uncompressed tone if there is one for the tone we wish
 * to play.
 *
 * @param mapper Instance of NgfToneMapper
 * @param tone Original tone
 * @return Full path to uncompressed tone
 */
static const char*
_get_mapped_tone (NgfToneMapper *mapper, const char *tone)
{
    const char *mapped_tone = NULL;

    if (mapper == NULL || tone == NULL)
        return NULL;

    mapped_tone = ngf_tone_mapper_get_tone (mapper, tone);
    if (mapped_tone) {
        LOG_DEBUG ("Tone (mapped): %s", mapped_tone);
        return mapped_tone;
    }

    LOG_DEBUG ("Tone (original): %s", tone);
    return tone;
}

static const char*
_event_get_tone (NgfEvent *self)
{
    NgfEventPrototype *proto = self->proto;
    NgfValue *value = NULL;
    const gchar *tone = NULL, *mapped_tone = NULL;

    value = g_hash_table_lookup (self->properties, "tone");
    if (value && ngf_value_get_type (value) == NGF_VALUE_STRING)
        tone = (const char*) ngf_value_get_string (value);

    if (tone == NULL && proto->tone_filename)
        tone = proto->tone_filename;

    if (tone == NULL)
        ngf_profile_get_string (self->context->profile, proto->tone_profile, proto->tone_key, &tone);

    return _get_mapped_tone (self->context->tone_mapper, tone);
}

static const char*
_event_get_fallback (NgfEvent *self)
{
    NgfEventPrototype *proto = self->proto;
    const gchar *tone = NULL, *mapped_tone = NULL;

    if (proto->fallback_filename)
        tone = proto->fallback_filename;

    if (tone == NULL)
        ngf_profile_get_string (self->context->profile, proto->fallback_profile, proto->fallback_key, &tone);

    return _get_mapped_tone (self->context->tone_mapper, tone);
}

static const char*
_event_get_vibra (NgfEvent *self)
{
    NgfEventPrototype *proto = self->proto;
    NgfValue *value = NULL;
    const gchar *vibra = NULL;

    value = g_hash_table_lookup (self->properties, "vibra");
    if (value && ngf_value_get_type (value) == NGF_VALUE_STRING)
        return (const char*) ngf_value_get_string (value);

    if (proto->vibrator_pattern)
        return (const char*) proto->vibrator_pattern;

    return NULL;
}

static const char*
_event_get_led (NgfEvent *self)
{
    NgfEventPrototype *proto = self->proto;
    NgfValue *value = NULL;
    const gchar *vibra = NULL;

    value = g_hash_table_lookup (self->properties, "led");
    if (value && ngf_value_get_type (value) == NGF_VALUE_STRING)
        return (const char*) ngf_value_get_string (value);

    if (proto->led_pattern)
        return (const char*) proto->led_pattern;

    return NULL;
}

static gboolean
_event_is_vibra_enabled (NgfEvent *self)
{
    gboolean enabled = FALSE;

    if (ngf_profile_get_boolean (self->context->profile, NULL, "vibrating.alert.enabled", &enabled))
        return enabled;

    return FALSE;
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

            LOG_DEBUG ("%s: STREAM FAILED\n", __FUNCTION__);

            /* Stream failed to start, most probably reason is that the file
               does not exist. In this case, if the fallback is specified we will
               try to play it. */

            if (!self->use_fallback) {
                self->use_fallback = TRUE;

                self->audio_id = ngf_audio_play_stream (self->context->audio,
                    _event_get_fallback (self), self->proto->stream_properties,
                    _stream_state_cb, self);

                break;
            }

            if (self->callback)
                self->callback (self, NGF_EVENT_FAILED, self->userdata);

            break;
        }

        case NGF_STREAM_TERMINATED: {

            LOG_DEBUG ("%s: STREAM TERMINATED\n", __FUNCTION__);

            /* Audio stream terminated. This means that the underlying audio context
               was lost (probably Pulseaudio went down). We will stop the event at
               this point (consider it completed). */

            if (self->callback)
                self->callback (self, NGF_EVENT_COMPLETED, self->userdata);

            break;
        }

        case NGF_STREAM_COMPLETED: {

            /* Stream was played and completed successfully. If the repeat flag is
               set, then we will restart the stream again. Otherwise, let's notify
               the user of the completion of the event. */

            const char *tone = NULL;

            if (self->proto->tone_repeat) {

                ++self->tone_repeat_count;

                if (self->proto->tone_repeat_count <= 0) {
                    LOG_DEBUG ("%s: STREAM REPEAT", __FUNCTION__);

                    tone = self->use_fallback ? _event_get_fallback (self) : _event_get_tone (self);
                    if (tone != NULL)
                        self->audio_id = ngf_audio_play_stream (self->context->audio, tone, self->proto->stream_properties, _stream_state_cb, self);
                }

                else if  (self->tone_repeat_count >= self->proto->tone_repeat_count) {
                    LOG_DEBUG ("%s: STREAM REPEAT FINISHED", __FUNCTION__);
                    self->audio_id = 0;
                    if (self->callback)
                        self->callback (self, NGF_EVENT_COMPLETED, self->userdata);
                }
            }
            else {
                LOG_DEBUG ("%s: STREAM COMPLETED", __FUNCTION__);
                self->audio_id = 0;
                if (self->callback)
                    self->callback (self, NGF_EVENT_COMPLETED, self->userdata);
            }

            break;
        }

        default:
            break;
    }
}

static gboolean
_volume_control_cb (guint id, guint step_time, guint step_value, gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;

    LOG_DEBUG ("VOLUME CONTROL SET VOLUME (id=%d, time=%d, value=%d)", id, step_time, step_value);
    ngf_audio_set_volume (self->context->audio, self->proto->volume_role, step_value);

    return TRUE;
}

static gboolean
_backlight_control_cb (guint id, guint step_time, guint step_value, gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;

    LOG_DEBUG ("BACKLIGHT CONTROL SET (id=%d, time=%d, value=%d)", id, step_time, step_value);
    ngf_backlight_toggle (self->context->backlight, step_value);

    return TRUE;
}

static gboolean
_setup_audio (NgfEvent *self)
{
    gint volume = 0;
    const char *tone = NULL;

    if (self->proto->tonegen_enabled) {
        self->tonegen_id = ngf_tonegen_start (self->context->tonegen, self->proto->tonegen_pattern);
        return TRUE;
    }

    if ((self->resources & NGF_RESOURCE_AUDIO) == 0)
        return FALSE;

    /* If we have a volume controller, it overrides all other volume settings. */
    if (self->proto->volume_controller) {
        self->controller_id = ngf_controller_start (self->proto->volume_controller,
            self->proto->volume_controller_repeat, _volume_control_cb, self);
    }
    else if ((volume = _event_get_volume (self)) > -1) {
        ngf_audio_set_volume (self->context->audio, self->proto->volume_role, volume);
    }

    if ((tone = _event_get_tone (self)) != NULL) {
        self->audio_id = ngf_audio_play_stream (self->context->audio,
            tone, self->proto->stream_properties, _stream_state_cb, self);

        if (self->audio_id == 0) {
            LOG_DEBUG ("%s: Failed to start tone, trying fallback.", __FUNCTION__);

            /* We couldn't start the provided tone. Let's try fallback if there is one. */
            if ((tone = _event_get_fallback (self)) != NULL) {
                self->audio_id = ngf_audio_play_stream (self->context->audio,
                    tone, self->proto->stream_properties, _stream_state_cb, self);

                self->use_fallback = TRUE;
            }
        }
    }

    return TRUE;
}

static void
_shutdown_audio (NgfEvent *self)
{
    if (self->tonegen_id > 0) {
        ngf_tonegen_stop (self->context->tonegen, self->tonegen_id);
        self->tonegen_id = 0;
    }

    if (self->controller_id > 0) {
        ngf_controller_stop (self->proto->volume_controller, self->controller_id);
        self->controller_id = 0;
    }

    if (self->audio_id > 0) {
        ngf_audio_stop_stream (self->context->audio, self->audio_id);
        self->audio_id = 0;
    }
}

static gboolean
_setup_vibrator (NgfEvent *self)
{
    const char *vibra = NULL;

    if (self->resources & NGF_RESOURCE_VIBRATION && _event_is_vibra_enabled (self)) {

        if ((vibra = _event_get_vibra (self)) != NULL)
            self->vibra_id = ngf_vibrator_start (self->context->vibrator, vibra);

        return TRUE;
    }

    return FALSE;
}

static void
_shutdown_vibrator (NgfEvent *self)
{
    if (self->vibra_id > 0) {
        ngf_vibrator_stop (self->context->vibrator, self->vibra_id);
        self->vibra_id = 0;
    }
}

static gboolean
_setup_led (NgfEvent *self)
{
    const char *led = NULL;

    if ((self->resources & NGF_RESOURCE_LED) == 0)
        return FALSE;

    if ((led = _event_get_led (self)) != NULL)
        self->led_id = ngf_led_start (self->context->led, led);

    return TRUE;
}

static void
_shutdown_led (NgfEvent *self)
{
    if (self->led_id > 0) {
        ngf_led_stop (self->context->led, self->led_id);
        self->led_id = 0;
    }
}

static gboolean
_setup_backlight (NgfEvent *self)
{
    if ((self->resources & NGF_RESOURCE_BACKLIGHT) == 0)
        return FALSE;

    if (self->proto->backlight_controller) {
        self->backlight_id = ngf_controller_start (self->proto->backlight_controller,
            self->proto->backlight_controller_repeat, _backlight_control_cb, self);
    }

    return TRUE;
}

static void
_shutdown_backlight (NgfEvent *self)
{
    if (self->backlight_id > 0) {
        ngf_controller_stop (self->proto->backlight_controller, self->backlight_id);
        self->backlight_id = 0;
    }
}

gboolean
ngf_event_start (NgfEvent *self, GHashTable *properties)
{
    const char *tone = NULL, *vibra = NULL, *led =  NULL;
    gint volume = 0;

    /* Take ownership of the properties */
    self->properties = properties;

    /* Check the resources and start the backends if we have the proper resources,
       profile allows us to and valid data is provided. */

    _setup_audio (self);
    _setup_vibrator (self);
    _setup_led (self);
    _setup_backlight (self);

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

    _shutdown_audio (self);
    _shutdown_vibrator (self);
    _shutdown_led (self);
    _shutdown_backlight (self);
}
