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

#include <glib.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gio/gio.h>

#define GST_KEY            "plugin.gst.data"
#define STREAM_PREFIX_KEY  "sound.stream."
#define LOG_CAT            "gst: "
#define SOUND_FILENAME_KEY "sound.filename"
#define SOUND_REPEAT_KEY   "sound.repeat"
#define SOUND_VOLUME_KEY   "sound.volume"

typedef struct _StreamData
{
    NRequest *request;
    NSinkInterface *iface;
    GstElement *pipeline;
    GstElement *volume;
    gboolean volume_linear;
    gboolean volume_limit;
    guint volume_cap;
    gint *volumes;
    guint num_volumes;
    GstStructure *properties;
    const gchar *filename;
    gboolean repeat_enabled;
    guint restart_source_id;
    gboolean first_play;
    GstController *controller;
    GstInterpolationControlSource *control_source;
    gdouble last_volume;
    gdouble time_spent;
    gboolean paused;
    guint bus_watch_id;
} StreamData;

N_PLUGIN_NAME        ("gst")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("GStreamer plugin")

static gchar* strip_prefix (const gchar *str, const gchar *prefix);
static gboolean parse_linear_volume (const char *str, gint **out_volumes, guint *out_num_volumes);
static gboolean parse_volume_limit (const char *str, guint *max);
static void free_linear_volume (gint *volumes);
static gboolean get_current_volume (StreamData *stream, gdouble *out_volume);
static gboolean get_current_position (StreamData *stream, gdouble *out_position);
static void set_stream_properties (GstElement *sink, const GstStructure *properties);
static int set_structure_string (GstStructure *s, const char *key, const char *value);
static void proplist_to_structure_cb (const char *key, const NValue *value, gpointer userdata);
static GstStructure* create_stream_properties (NProplist *props);
static gboolean restart_stream_cb (gpointer userdata);
static gboolean bus_cb (GstBus *bus, GstMessage *msg, gpointer userdata);
static void new_decoded_pad_cb (GstElement *element, GstPad *pad, gboolean is_last, gpointer userdata);
static int make_pipeline (StreamData *stream);
static void free_pipeline (StreamData *stream);

static gboolean system_sounds_enabled = TRUE;
static guint system_sounds_level = 0;

static gchar*
strip_prefix (const gchar *str, const gchar *prefix)
{
    if (!g_str_has_prefix (str, prefix))
        return NULL;

    size_t prefix_length = strlen (prefix);
    return g_strdup (str + prefix_length);
}

static gboolean
parse_linear_volume (const char *str, gint **out_volumes, guint *out_num_volumes)
{
    gchar *stripped = NULL;
    gchar **split = NULL, **item = NULL;
    guint num_volumes = 0, i = 0;
    gint *volumes = NULL;
    gboolean valid_volume = TRUE;

    if (!str || !g_str_has_prefix (str, "linear:"))
        return FALSE;

    stripped = strip_prefix (str, "linear:");
    split = g_strsplit (stripped, ";", -1);
    if (split[0] == NULL) {
        valid_volume = FALSE;
        goto done;
    }

    for (item = split, num_volumes = 0; *item != NULL; ++item, ++num_volumes) ;

    if (num_volumes != 3) {
        N_WARNING (LOG_CAT "invalid volume definition");
        valid_volume = FALSE;
        goto done;
    }

    if (num_volumes > 0) {
        volumes = g_malloc0 (sizeof (gint) * num_volumes);
        for (i = 0; i < num_volumes; ++i)
            volumes[i] = atoi (split[i]);

        *out_volumes = volumes;
        *out_num_volumes = num_volumes;
    }

done:
    g_strfreev (split);
    g_free (stripped);
    return valid_volume;
}

static gboolean
parse_volume_limit (const char *str, guint *max)
{
    gchar *stripped = NULL;

    if (!str || !g_str_has_prefix (str, "max:"))
        return FALSE;

    stripped = strip_prefix (str, "max:");
    *max = atoi (stripped);

    return TRUE;
}

static void
free_linear_volume (gint *volumes)
{
    if (volumes)
        g_free (volumes);
}

static gboolean
get_current_volume (StreamData *stream, gdouble *out_volume)
{
    gdouble v;

    g_object_get (G_OBJECT (stream->volume),
        "volume", &v, NULL);

    *out_volume = v;

    return TRUE;
}

static gboolean
get_current_position (StreamData *stream, gdouble *out_position)
{
    GstFormat fmt = GST_FORMAT_TIME;
    gint64 timestamp;

    if (!gst_element_query_position (stream->pipeline, &fmt, &timestamp)) {
        N_WARNING (LOG_CAT "unable to query data position");
        return FALSE;
    }

    if (!(GST_CLOCK_TIME_IS_VALID (timestamp) && fmt == GST_FORMAT_TIME)) {
        N_WARNING (LOG_CAT "queried position or format is not valid");
        return FALSE;
    }

    *out_position = (gdouble) timestamp / GST_SECOND;
    return TRUE;
}

static void
set_stream_properties (GstElement *sink, const GstStructure *properties)
{
    if (!sink | !properties)
        return;

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "stream-properties") != NULL) {
        g_object_set (G_OBJECT (sink), "stream-properties", properties, NULL);
    }
}

static int
set_structure_string (GstStructure *s, const char *key, const char *value)
{
    g_assert (s != NULL);
    g_assert (key != NULL);

    GValue v = {0,{{0}}};

    if (!value)
        return FALSE;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    gst_structure_set_value (s, key, &v);
    g_value_unset (&v);

    return TRUE;
}

static void
proplist_to_structure_cb (const char *key, const NValue *value, gpointer userdata)
{
    GstStructure *target     = (GstStructure*) userdata;
    const char   *prop_key   = NULL;
    const char   *prop_value = NULL;

    if (!g_str_has_prefix (key, STREAM_PREFIX_KEY))
        return;

    prop_key = key + strlen (STREAM_PREFIX_KEY);
    if (*prop_key == '\0')
        return;

    prop_value = n_value_get_string ((NValue*) value);
    (void) set_structure_string (target, prop_key, prop_value);
}

static GstStructure*
create_stream_properties (NProplist *props)
{
    g_assert (props != NULL);

    GstStructure *s      = gst_structure_empty_new ("props");
    const char   *source = NULL;
    const char   *role   = NULL;

    /* set the stream filename based on the sound file we're
       about to play. */

    source = n_proplist_get_string (props, SOUND_FILENAME_KEY);
    g_assert (source != NULL);

    set_structure_string (s, "media.filename", source);

    /* set a media.role to "media" */

    set_structure_string (s, "media.role", "media");

    /* if system sound level is off and the flag is set, then we need to
       use different stream restore role. */

    role = n_proplist_get_string (props ,"system-sounds-role");
    if (!system_sounds_enabled && role) {
        N_DEBUG (LOG_CAT "system sounds are off and replace role is set, using '%s'", role);
        n_proplist_set_string (props, STREAM_PREFIX_KEY "module-stream-restore.id", role);
    }

    /* convert all properties within the request that begin with
       "sound.stream." prefix. */

    n_proplist_foreach (props, proplist_to_structure_cb, s);

    return s;
}

static void
free_stream_properties (GstStructure *s)
{
    if (s)
        gst_structure_free (s);
}

static int
create_volume (StreamData *stream)
{
    GstController *controller = NULL;
    GstInterpolationControlSource *control_source = NULL;
    GValue v = {0,{{0}}};
    gdouble time_left = 0.0;

    if (stream->volume_linear) {
        time_left = stream->volumes[2] - stream->time_spent;
        if (time_left <= 0.0) {
            N_DEBUG (LOG_CAT "volume controller done.");
            g_object_set (G_OBJECT (stream->volume), "volume", stream->volumes[1] / 100.0, NULL);
            return FALSE;
        }

        /* create controller and set values */

        N_DEBUG (LOG_CAT "linear volume enabled, raising volume from %.2f to %.2f in %.2f seconds.",
            stream->last_volume, stream->volumes[1] / 100.0, time_left);

        controller = gst_controller_new (G_OBJECT (stream->volume), "volume", NULL);
        control_source = gst_interpolation_control_source_new ();
        gst_controller_set_control_source (controller, "volume", GST_CONTROL_SOURCE (control_source));
        gst_interpolation_control_source_set_interpolation_mode (control_source, GST_INTERPOLATE_LINEAR);

        g_value_init (&v, G_TYPE_DOUBLE);
        g_value_set_double (&v, stream->last_volume);
        gst_interpolation_control_source_set ((GstInterpolationControlSource*) control_source,
            0, &v); /* start_time */

        g_value_reset (&v);
        g_value_set_double (&v, stream->volumes[1] / 100.0);
        gst_interpolation_control_source_set ((GstInterpolationControlSource*) control_source,
            time_left * GST_SECOND, &v);

        g_value_unset (&v);

        gst_controller_set_disabled (controller, FALSE);

        stream->controller = controller;
        stream->control_source = control_source;

        return TRUE;
    }

    if (stream->volume_limit) {
        if (system_sounds_level > stream->volume_cap) {
            g_object_set (G_OBJECT (stream->volume), "volume", stream->volume_cap / 100.0, NULL);
        }

        return TRUE;
    }

    return FALSE;
}

static void
free_volume (StreamData *stream)
{
    if (stream->controller) {
        g_object_unref (G_OBJECT (stream->controller));
        stream->controller = NULL;
        stream->control_source = NULL;
    }
}

static gboolean
restart_stream_cb (gpointer userdata)
{
    StreamData *stream = (StreamData*) userdata;
    gdouble position = 0.0;

    stream->restart_source_id = 0;
    stream->first_play = FALSE;

    /* query the current position and volume */

    (void) get_current_position (stream, &position);
    stream->time_spent += position;

    (void) get_current_volume (stream, &stream->last_volume);

    /* stop the previous stream and free it */

    free_pipeline (stream);
 
    /* recreate the stream */

    N_DEBUG (LOG_CAT "re-creating pipeline.");
    if (!make_pipeline (stream)) {
        n_sink_interface_fail (stream->iface, stream->request);
        return FALSE;
    }

    /* put the stream to the playing state immediately */

    N_DEBUG (LOG_CAT "setting pipeline to playing");
    gst_element_set_state (stream->pipeline, GST_STATE_PLAYING);

    return FALSE;
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer userdata)
{
    StreamData *stream = (StreamData*) userdata;

    (void) bus;

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR: {
            GError *error = NULL;
            gst_message_parse_error (msg, &error, NULL);
            N_WARNING (LOG_CAT "error: %s", error->message);
            g_error_free (error);
            n_sink_interface_fail (stream->iface, stream->request);
            return FALSE;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

            if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
                N_DEBUG (LOG_CAT "synchronize");
                n_sink_interface_synchronize (stream->iface, stream->request);
            }

            break;
        }

        case GST_MESSAGE_EOS: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            if (stream->repeat_enabled) {
                N_DEBUG (LOG_CAT "rewinding pipeline.");
                n_sink_interface_resynchronize (stream->iface, stream->request);
                stream->restart_source_id = g_idle_add (restart_stream_cb, stream);
                return FALSE;
            }

            N_DEBUG (LOG_CAT "eos");
            n_sink_interface_complete (stream->iface, stream->request);
            return FALSE;
        }

        default:
            break;
    }

    return TRUE;
}

static void
new_decoded_pad_cb (GstElement *element, GstPad *pad, gboolean is_last,
                    gpointer userdata)
{
    GstElement   *sink_element = (GstElement*) userdata;
    GstStructure *structure    = NULL;
    GstCaps      *caps         = NULL;
    GstPad       *sink_pad     = NULL;

    (void) element;
    (void) is_last;

    caps = gst_pad_get_caps (pad);
    if (gst_caps_is_empty (caps) || gst_caps_is_any (caps)) {
        gst_caps_unref (caps);
        return;
    }

    structure = gst_caps_get_structure (caps, 0);
    if (g_str_has_prefix (gst_structure_get_name (structure), "audio")) {
        sink_pad = gst_element_get_pad (sink_element, "sink");
        if (!gst_pad_is_linked (sink_pad))
            gst_pad_link (pad, sink_pad);
        gst_object_unref (sink_pad);
    }

    gst_caps_unref (caps);
}

static int
make_pipeline (StreamData *stream)
{
    GstElement *pipeline = NULL, *source = NULL, *decoder = NULL,
        *audioconv = NULL, *volume = NULL, *sink = NULL;
    GstBus *bus = NULL;

    pipeline = gst_pipeline_new (NULL);
    source = gst_element_factory_make ("filesrc", NULL);
    decoder = gst_element_factory_make ("decodebin2", NULL);
    audioconv = gst_element_factory_make ("audioconvert", NULL);
    volume = gst_element_factory_make ("volume", NULL);
    sink = gst_element_factory_make ("pulsesink", NULL);

    if (!pipeline || !source || !decoder || !volume || !sink) {
        N_WARNING (LOG_CAT "failed to create required elements.");
        goto failed;
    }

    gst_bin_add_many (GST_BIN (pipeline), source, decoder, audioconv, volume, sink, NULL);
    
    if (!gst_element_link (source, decoder)) {
        N_WARNING (LOG_CAT "failed to link source to decoder");
        goto failed_pipeline;
    }

    if (!gst_element_link_many (audioconv, volume, sink, NULL)) {
        N_WARNING (LOG_CAT "failed to link converter, volume or sink");
        goto failed_pipeline;
    }

    g_signal_connect (G_OBJECT (decoder), "new-decoded-pad",
        G_CALLBACK (new_decoded_pad_cb), audioconv);

    g_object_set (G_OBJECT (source), "location", stream->filename, NULL);

    bus = gst_element_get_bus (pipeline);
    stream->bus_watch_id = gst_bus_add_watch (bus, bus_cb, stream);
    gst_object_unref (bus);

    set_stream_properties (sink, stream->properties);

    stream->pipeline = pipeline;
    stream->volume = volume;

    (void) create_volume (stream);

    return TRUE;

failed:
    free_volume (stream);

    if (sink)
        gst_object_unref (sink);
    if (volume)
        gst_object_unref (volume);
    if (audioconv)
        gst_object_unref (audioconv);
    if (decoder)
        gst_object_unref (decoder);
    if (source)
        gst_object_unref (source);

failed_pipeline:
    if (pipeline)
        gst_object_unref (pipeline);

    return FALSE;
}

static void
free_pipeline (StreamData *stream)
{
    free_volume (stream);

    if (stream->bus_watch_id > 0) {
        g_source_remove (stream->bus_watch_id);
        stream->bus_watch_id = 0;
    }

    if (stream->pipeline) {
        N_DEBUG (LOG_CAT "freeing pipeline");
        gst_element_set_state (stream->pipeline, GST_STATE_NULL);
        gst_object_unref (stream->pipeline);
        stream->pipeline = NULL;
    }
}

static void
system_sound_level_changed (NContext *context,
                            const char *key,
                            const NValue *old_value,
                            const NValue *new_value,
                            void *userdata)
{
    (void) context;
    (void) key;
    (void) old_value;
    (void) userdata;

    int v = 0;

    if (new_value) {
        v = n_value_get_int (new_value);
        system_sounds_level = v;
        if (v <= 0 && system_sounds_enabled) {
            N_DEBUG (LOG_CAT "system sounds are disabled.");
            system_sounds_enabled = FALSE;
        }
        else if (!system_sounds_enabled) {
            N_DEBUG (LOG_CAT "system sounds are enabled.");
            system_sounds_enabled = TRUE;
        }
    }
}

static void
init_done_cb (NHook *hook, void *data, void *userdata)
{
    (void) hook;
    (void) data;

    NContext *context = (NContext*) userdata;
    NValue *v = NULL;

    /* query the initial system sound level value */
    v = (NValue*) n_context_get_value (context, "profile.current.system.sound.level");
    if (v)
        system_sounds_enabled = n_value_get_int (v) > 0 ? TRUE : FALSE;

    if (!n_context_subscribe_value_change (context,
            "profile.current.system.sound.level",
            system_sound_level_changed, NULL))
    {
        N_WARNING (LOG_CAT "failed to subscribe to system sound "
                           "volume change");
    }
}

static int
gst_sink_initialize (NSinkInterface *iface)
{
    (void) iface;

    N_DEBUG (LOG_CAT "initializing GStreamer");

    gst_init_check (NULL, NULL, NULL);
    gst_controller_init (NULL, NULL);

    return TRUE;
}

static void
gst_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
}

static int
gst_sink_can_handle (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    NProplist *props = NULL;

    props = (NProplist*) n_request_get_properties (request);
    if (n_proplist_has_key (props, SOUND_FILENAME_KEY)) {
        N_DEBUG (LOG_CAT "request has a sound.filename, we can handle this.");
        return TRUE;
    }

    return FALSE;
}

static int
gst_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    StreamData *stream = NULL;
    NProplist *props = NULL;

    props = (NProplist*) n_request_get_properties (request);

    stream = g_slice_new0 (StreamData);
    stream->request = request;
    stream->iface = iface;
    stream->filename = n_proplist_get_string (props, SOUND_FILENAME_KEY);
    stream->repeat_enabled = n_proplist_get_bool (props, SOUND_REPEAT_KEY);
    stream->properties = create_stream_properties (props);
    stream->first_play = TRUE;

    stream->volume_linear = parse_linear_volume (
        n_proplist_get_string (props, SOUND_VOLUME_KEY), &stream->volumes, &stream->num_volumes);

    stream->volume_limit = parse_volume_limit (n_proplist_get_string (props, SOUND_VOLUME_KEY),
        &stream->volume_cap);

    n_request_store_data (request, GST_KEY, stream);

    if (!make_pipeline (stream))
        return FALSE;

    N_DEBUG (LOG_CAT "setting pipeline to paused");
    gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);

    return TRUE;
}

static int
gst_sink_play (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    if (stream->pipeline) {
        N_DEBUG (LOG_CAT "setting pipeline to playing");
        gst_element_set_state (stream->pipeline, GST_STATE_PLAYING);
        stream->paused = FALSE;
    }

    return TRUE;
}

static int
gst_sink_pause (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    if (stream->pipeline && !stream->paused) {
        N_DEBUG (LOG_CAT "pausing pipeline.");
        gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);
        stream->paused = TRUE;
    }

    return TRUE;
}

static void
gst_sink_stop (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    StreamData *stream = NULL;

    stream = (StreamData*) n_request_get_data (request, GST_KEY);
    g_assert (stream != NULL);

    free_linear_volume (stream->volumes);
    stream->volumes = NULL;

    if (stream->restart_source_id > 0) {
        g_source_remove (stream->restart_source_id);
        stream->restart_source_id = 0;
    }

    free_pipeline (stream);
    free_stream_properties (stream->properties);

    g_slice_free (StreamData, stream);
}

N_PLUGIN_LOAD (plugin)
{
    NCore    *core    = NULL;
    NContext *context = NULL;

    static const NSinkInterfaceDecl decl = {
        .name       = "gst",
        .initialize = gst_sink_initialize,
        .shutdown   = gst_sink_shutdown,
        .can_handle = gst_sink_can_handle,
        .prepare    = gst_sink_prepare,
        .play       = gst_sink_play,
        .pause      = gst_sink_pause,
        .stop       = gst_sink_stop
    };

    n_plugin_register_sink (plugin, &decl);

    core = n_plugin_get_core (plugin);
    context = n_core_get_context (core);

    if (!n_core_connect (core, N_CORE_HOOK_INIT_DONE, 0,
                         init_done_cb, context))
    {
        N_WARNING (LOG_CAT "failed to setup init done hook.");
    }

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    NCore    *core    = NULL;
    NContext *context = NULL;

    core = n_plugin_get_core (plugin);
    context = n_core_get_context (core);

    n_context_unsubscribe_value_change (context,
        "profile.current.system.sound.level",
        system_sound_level_changed);

    n_core_disconnect (core, N_CORE_HOOK_INIT_DONE,
        init_done_cb, context);
}
