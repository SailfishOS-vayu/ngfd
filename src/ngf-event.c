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
 
#include <string.h>

#include "ngf-log.h"
#include "ngf-value.h"
#include "ngf-properties.h"
#include "ngf-tone-mapper.h"
#include "ngf-controller.h"
#include "ngf-event.h"

static inline void  _trigger_event_callback (NgfEvent *event, NgfEventState  state);
static gboolean     _max_timeout_triggered_cb (gpointer userdata);

static void         _stream_state_cb (NgfAudioStream *stream, NgfAudioStreamState state, gpointer userdata);
static void         _interface_ready_cb (NgfInterfaceType type, gboolean success, gpointer userdata);
static const char*  _get_mapped_tone (NgfToneMapper *mapper, const char *tone);

static gboolean     _tone_generator_start (NgfEvent *self);
static void         _tone_generator_stop (NgfEvent *self);

static void         _set_stream_volume  (NgfEvent *self);
static void         _clear_stream_volume (NgfEvent *self);

static gboolean     _audio_playback_start (NgfEvent *self);
static void         _audio_playback_stop (NgfEvent *self);

static gboolean     _setup_vibrator (NgfEvent *self);
static gboolean     _poll_vibrator (gpointer userdata);
static gboolean     _setup_led (NgfEvent *self);
static gboolean     _setup_backlight (NgfEvent *self);
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

    self->context     = context;
    self->proto       = proto;
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
    
    if (self->vibra_data) {
        g_free (self->vibra_data);
        self->vibra_data = NULL;
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
_max_timeout_triggered_cb (gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;

    self->max_length_timeout_id = 0;
    ngf_event_stop (self);

    _trigger_event_callback (self, NGF_EVENT_COMPLETED);
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

    return NULL;
}

/**
 * Trigger the user specified callback for the event. If no callback
 * specified, nothing is done.
 *
 * @param event NgfEvent
 * @param state NgfEventState
 * @post User provided callback is triggered with given state and userdata.
 */

static inline void
_trigger_event_callback (NgfEvent      *event,
                         NgfEventState  state)
{
    if (event->callback)
        event->callback (event, state, event->userdata);
}

/**
 * Interface ready callback is called when backend is ready to start
 * the event. Used to synchronize starting of backends at the same 
 * time.
 *
 * @param type NgfInterfaceType
 * @param success gboolean
 * @param userdata Userdata
 */

static void
_interface_ready_cb (NgfInterfaceType type, gboolean success, gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;
    const char *vibra = NULL;
    
    switch (type) {
        case NGF_INTERFACE_AUDIO:
            if (success) {
                LOG_DEBUG ("Audio backend ready");
                self->audio_ready = TRUE;
            } else {
                _stream_state_cb (self->audio_stream, NGF_AUDIO_STREAM_STATE_FAILED, self);
            }
            break;
        default:
            break;
    }
    
    if (ngf_properties_get_bool (self->properties, "audio_enabled")) {
        if (!self->audio_ready)
            return;
    }
    
    LOG_DEBUG ("All backends ready, starting event");
    if (ngf_properties_get_bool (self->properties, "audio_enabled")) {
        if (!ngf_audio_play (self->context->audio, self->audio_stream))
            ngf_audio_destroy_stream (self->context->audio, self->audio_stream);
    }
    
    if (self->resources & NGF_RESOURCE_VIBRATION && ngf_profile_is_vibra_enabled (self->context->profile)) {
        if (self->vibra_data) { 
            self->vibra_id = ngf_vibrator_start (self->context->vibrator, NULL, self->vibra_data);
        } else {
            vibra = ngf_properties_get_string (self->properties, "vibra");
            if (vibra != NULL)
                self->vibra_id = ngf_vibrator_start (self->context->vibrator, vibra, NULL);
        }
        
        if (self->vibra_id && ngf_profile_is_silent (self->context->profile) && 
            !ngf_vibrator_is_repeating (self->context->vibrator, vibra)) {
            /* If we are in silent mode, set callback to monitor when pattern is complete, if pattern is non-repeating one */
            self->vibra_poll_id = g_timeout_add (NGF_VIBRA_POLL_TIMEOUT, _poll_vibrator, self);
        }
    }

    if (ngf_properties_get_bool (self->properties, "led_enabled"))
        _setup_led (self);

    if (ngf_properties_get_bool (self->properties, "backlight_enabled"))
        _setup_backlight (self);
}

/**
 * Stream state callback is triggered by the audio backend to inform us
 * of the streams state.
 *
 * @param stream NgfAudioStream
 * @param state NgfAudioStreamState
 * @param userdata Userdata
 */

static void
_stream_state_cb (NgfAudioStream *stream, NgfAudioStreamState state, gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;

    NgfEventState callback_state = NGF_EVENT_NONE;
    gboolean      restart_stream = FALSE;

    switch (state) {
        case NGF_AUDIO_STREAM_STATE_STARTED:
            callback_state = NGF_EVENT_STARTED;
            break;

        case NGF_AUDIO_STREAM_STATE_FAILED: {

            /* If stream was a fallback stream and it failed, then trigger the
               callback and finish the event. */

            _audio_playback_stop (self);

            if (self->audio_use_fallback) {
                callback_state = NGF_EVENT_FAILED;
                break;
            }

            self->audio_use_fallback = TRUE;
            restart_stream           = TRUE;

            break;
        }

        case NGF_AUDIO_STREAM_STATE_COMPLETED: {

            _audio_playback_stop (self);

            if (self->audio_repeat_enabled) {
                self->audio_repeat_count++;

                if (self->audio_max_repeats <= 0 || self->audio_repeat_count < self->audio_max_repeats) {
                    restart_stream = TRUE;
                    break;
                }
            }

            callback_state = NGF_EVENT_COMPLETED;
            break;
        }

        default:
            break;
    }

    if (restart_stream)
        _audio_playback_start (self);

    if (callback_state != NGF_EVENT_NONE)
        _trigger_event_callback (self, callback_state);
}

static gboolean
_tone_generator_start (NgfEvent *self)
{
    if (ngf_properties_get_bool (self->properties, "audio_tonegen_enabled")) {
        self->tonegen_id = ngf_tonegen_start (self->context->tonegen, ngf_properties_get_int (self->properties, "audio_tonegen_pattern"));
        return TRUE;
    }

    return FALSE;
}

static void
_tone_generator_stop (NgfEvent *self)
{
    if (self->tonegen_id > 0) {
        ngf_tonegen_stop (self->context->tonegen, self->tonegen_id);
        self->tonegen_id = 0;
    }
}

static gboolean
_volume_controller_cb (NgfController *controller,
                       guint          id,
                       guint          step_time,
                       guint          step_value,
                       gpointer       userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;
    ngf_audio_set_volume (self->context->audio, ngf_properties_get_string (self->properties, "audio_stream_role"), step_value);
    return TRUE;
}

/**
 * Get audio filename from the the properties or if no such file, from
 * the profiles.
 *
 * @param self NgfEvent
 * @param filename_key Property key
 * @param profile_key Profile key
 * @returns Audio filename, do not free.
 */

static const char*
_get_audio_filename (NgfEvent   *self,
                     const char *filename_key,
                     const char *profile_key)
{
    const char *filename = NULL;
    const char *profile  = NULL;

    g_assert (self != NULL);

    if ((filename = ngf_properties_get_string (self->properties, filename_key)) != NULL)
        return filename;

    if ((profile = ngf_properties_get_string (self->properties, profile_key)) != NULL) {
        return ngf_profile_get_string_from_key (self->context->profile, profile);
    }

    return NULL;
}

/**
 * Get audio volume from the properties or from the profiles.
 *
 * @param self NgfEvent
 * @param volume_key Property key
 * @param profile_key Profile key
 * @returns Audio volume or -1 if no volume.
 */

static gint
_get_audio_volume (NgfEvent   *self,
                   const char *volume_key,
                   const char *profile_key)
{
    gint volume = 0;
    const char *profile = NULL;

    g_assert (self != NULL);

    if ((profile = ngf_properties_get_string (self->properties, profile_key)) != NULL) {
        return ngf_profile_get_int_from_key (self->context->profile, profile);
    }

    if ((volume = ngf_properties_get_int (self->properties, volume_key)) > -1)
        return volume;

    return -1;
}

static void
_set_stream_volume (NgfEvent *self)
{
    const char *pattern = NULL;
    gint        volume  = 0;

    if (self->audio_volume_set)
        return;

    pattern = ngf_properties_get_string (self->properties, "audio_volume_pattern");
    volume  = _get_audio_volume (self, "audio_volume_value", "audio_volume_profile");

    if (pattern != NULL) {
        self->audio_volume_controller = ngf_audio_get_controller (self->context->audio, pattern);
        self->audio_volume_id = ngf_controller_start (self->audio_volume_controller, _volume_controller_cb, self);
    }
    else if (volume >= 0) {
        ngf_audio_set_volume (self->context->audio, ngf_properties_get_string (self->properties, "audio_stream_role"), volume);
    }

    self->audio_volume_set = TRUE;
 }

static void
_clear_stream_volume (NgfEvent *self)
{
    if (self->audio_volume_id > 0) {
        ngf_controller_stop (self->audio_volume_controller, self->audio_volume_id);
        self->audio_volume_controller = NULL;
        self->audio_volume_id = 0;
    }

    self->audio_volume_set = FALSE;
}

static gboolean
_audio_playback_start (NgfEvent *self)
{
    NgfEventPrototype  *prototype   = self->proto;
    const char         *mapped      = NULL;
    const char         *source      = NULL;
    NgfAudioStreamType  stream_type = 0;
    NgfAudioStream     *stream      = NULL;

    if ((self->resources & NGF_RESOURCE_AUDIO) == 0)
        return FALSE;

    /* If we are in the silent mode and the prototype's audio_silent
       flag has not been set, nothing to do here. */

    if (ngf_profile_is_silent (self->context->profile) && !ngf_properties_get_bool (self->properties, "audio_silent"))
        return FALSE;

    if (self->audio_use_fallback)
        self->audio_filename = _get_audio_filename (self, "audio_fallback_filename", "audio_fallback_profile");

    source = self->audio_filename;

    /* set the stream volume */
    _set_stream_volume (self);

    /* If we tried to get fallback and it did not exist, nothing to
       play here. */

    if (source == NULL && self->audio_use_fallback)
        return FALSE;

    /* There was no tone available, use the fallback and try again. */

    if (source == NULL && !self->audio_use_fallback) {
        self->audio_use_fallback = TRUE;
        return _audio_playback_start (self);
    }

    /* Get the mapped (uncompressed) filename, if such thing exists and
       set the stream type to uncompressed. */

    mapped = _get_mapped_tone (self->context->tone_mapper, source);
    if (mapped != NULL) {
        source = mapped;
        stream_type = NGF_AUDIO_STREAM_UNCOMPRESSED;
    }

    /* Create a new audio stream and set it's properties. */

    stream = ngf_audio_create_stream (self->context->audio, stream_type);
    stream->source     = g_strdup (source);
    stream->properties = pa_proplist_copy (prototype->stream_properties);
    stream->iface_callback = _interface_ready_cb;
    stream->callback   = _stream_state_cb;
    stream->userdata   = self;

    /* Prepare the stream and start it's playback. */

    if (!ngf_audio_prepare (self->context->audio, stream)) {
        ngf_audio_destroy_stream (self->context->audio, stream);
        return FALSE;
    }

    self->audio_stream = stream;
    return TRUE;
}

static void
_audio_playback_stop (NgfEvent *self)
{
    _clear_stream_volume (self);

    if (self->audio_stream != NULL) {
        ngf_audio_stop (self->context->audio, self->audio_stream);
        ngf_audio_destroy_stream (self->context->audio, self->audio_stream);
        self->audio_stream = NULL;
    }
}

static gboolean
_poll_vibrator (gpointer userdata)
{
    NgfEvent *self = (NgfEvent*) userdata;
    
    if (self) {
        if (ngf_vibrator_is_completed (self->context->vibrator, self->vibra_id)) {
            _trigger_event_callback (self, NGF_EVENT_COMPLETED);
            self->vibra_id = 0;
            self->vibra_poll_id = 0;
            return FALSE;
        } else
            return TRUE;
    }
    
    self->vibra_poll_id = 0;
    return FALSE;
}

static gchar* 
_get_ivt_filename (const char *source)
{
    gchar *separator = NULL;
    gchar *output = NULL;
    size_t size;

    if (source == NULL)
        return NULL;
    
    separator = g_strrstr (source, ".");
    size = (separator != NULL) ? (size_t) (separator - source) : strlen (source);

    if (size == 0)
        return NULL;

    output = g_try_malloc0 (size + 5);
    if (output == NULL)
        return NULL;
    
    strncpy (output, source, size);
    strncat (output, ".ivt", 4);

    return output;
}

static gboolean
_setup_vibrator (NgfEvent *self)
{
    gchar   *ivtfile = NULL;

    if (self->resources & NGF_RESOURCE_VIBRATION && ngf_profile_is_vibra_enabled (self->context->profile)) {
        /* If vibrator_custom_patterns property is sent and with current ringtone file exists file with same
           name, but with .ivt extension, use that file as vibration pattern. */

        if (ngf_properties_get_bool (self->properties, "vibrator_custom_patterns") && self->audio_filename) {
            LOG_DEBUG ("Custom vibration patterns are enabled.");
            ivtfile = _get_ivt_filename (self->audio_filename);
            if (ivtfile && g_file_test (ivtfile, G_FILE_TEST_EXISTS)) {
                LOG_DEBUG ("%s: Loading vibration with custom pattern file %s.", __FUNCTION__, ivtfile);
                self->vibra_data = ngf_vibrator_load (ivtfile);
            }

            g_free (ivtfile);
            ivtfile = NULL;
        }       

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

    if ((led = ngf_properties_get_string (self->properties, "led")) != NULL)
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
    
    return ngf_backlight_start (self->context->backlight, ngf_properties_get_bool (self->properties, "unlock_tklock"));
}

static void
_shutdown_backlight (NgfEvent *self)
{
    if (self->context->backlight && (self->resources & NGF_RESOURCE_BACKLIGHT))
        ngf_backlight_stop (self->context->backlight);
}

gboolean
ngf_event_start (NgfEvent *self, GHashTable *properties)
{
    NgfEventPrototype *p = self->proto;

    /* If override is allowed, make a copy of the prototype's property hash table and merge our
       custom allowed properties in. */

    self->properties = ngf_properties_copy (p->properties);
    if (!ngf_properties_get_bool (self->properties, "disallow_override")) {
        LOG_DEBUG ("Override allowed, merging properties.");
        ngf_properties_merge_allowed (self->properties, properties, p->allowed_keys);
    }
    else {
        LOG_DEBUG ("Override is not allowed.");
    }

    LOG_DEBUG ("<event properties>");
    ngf_properties_dump (self->properties);

    /* Fetch some properties and save as internal data */

    self->audio_repeat_enabled = ngf_properties_get_bool (self->properties, "audio_repeat");
    self->audio_max_repeats    = ngf_properties_get_int  (self->properties, "audio_max_repeats");
    self->audio_filename       = _get_audio_filename (self, "audio", "audio_tone_profile");

    /* Check the resources and start the backends if we have the proper resources,
       profile allows us to and valid data is provided. */

    if (ngf_properties_get_bool (self->properties, "audio_enabled")) {
        if (!_tone_generator_start (self))
            _audio_playback_start (self);
    }

    if (ngf_properties_get_bool (self->properties, "vibra_enabled"))
        _setup_vibrator (self);

    /* Timeout callback for maximum length of the event. Once triggered we will
       stop the event ourselves. */

    guint max_length = ngf_properties_get_int (self->properties, "max_length");
    if (max_length > 0)
        self->max_length_timeout_id = g_timeout_add (max_length, _max_timeout_triggered_cb, self);

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
    
    if (self->vibra_poll_id > 0) {
        g_source_remove (self->vibra_poll_id);
        self->vibra_poll_id = 0;
    }

    _tone_generator_stop (self);
    _audio_playback_stop (self);

    _shutdown_vibrator (self);
    _shutdown_led (self);
    _shutdown_backlight (self);
}
