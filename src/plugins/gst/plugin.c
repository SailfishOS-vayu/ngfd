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

#define GST_KEY           "plugin.gst.data"
#define STREAM_PREFIX_KEY "sound.stream."
#define LOG_CAT           "gst: "

static gint   _gst_plugin_buffer_time  = -1;
static gint   _gst_plugin_latency_time = -1;

typedef struct _GstData
{
    NRequest                      *request;
    NSinkInterface                *iface;

    GstElement                    *pipeline;
    GstElement                    *volume_element;
    gboolean                       has_linear_volume;
    gint                           linear_volume[3];
    GstController                 *controller;
    GstInterpolationControlSource *csource;
    const gchar                   *source;
    GstStructure                  *properties;

    gdouble                        time_played;
    gint                           buffer_time;
    gint                           latency_time;
    gboolean                       paused;
    gboolean                       repeating;
} GstData;

N_PLUGIN_NAME        ("gst")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("GStreamer plugin")

static void          reset_linear_volume        (GstData *data, gboolean query_position);
static void          pipeline_rewind            (GstElement *pipeline, gboolean flush);
static gboolean      bus_cb                     (GstBus *bus, GstMessage *msg, gpointer userdata);
static void          new_decoded_pad_cb         (GstElement *element, GstPad *pad, gboolean is_last, gpointer userdata);
static void          set_stream_properties      (GstElement *sink, const GstStructure *properties);
static int           set_structure_string       (GstStructure *s, const char *key, const char *value);
static void          convert_stream_property_cb (const char *key, const NValue *value, gpointer userdata);
static GstStructure* create_stream_properties   (NProplist *props);



static gchar*
strip_prefix (const gchar *str, const gchar *prefix)
{
    if (!g_str_has_prefix (str, prefix))
        return NULL;

    size_t prefix_length = strlen (prefix);
    return g_strdup (str + prefix_length);
}

static void
parse_linear_volume (GstData *data, const char *str)
{
    gchar *stripped = NULL;
    gchar **split = NULL, **item = NULL;
    gint i = 0;

    if (!str || !g_str_has_prefix (str, "linear:"))
        return;

    stripped = strip_prefix (str, "linear:");
    split = g_strsplit (stripped, ";", -1);
    if (split[0] == NULL) {
        g_strfreev (split);
        g_free (stripped);
        return;
    }

    item = split;
    for (i = 0; i < 3; i++) {
        if (*item == NULL) {
            g_strfreev (split);
            g_free (stripped);
            return;
        }

        data->linear_volume[i] = atoi (*item);
        item++;
    }

    data->has_linear_volume = TRUE;

    g_strfreev (split);
    g_free (stripped);
}

static gboolean
get_current_volume (GstData *data, gdouble *out_volume)
{
    gdouble v;

    if (!data->volume_element)
        return FALSE;

    g_object_get (G_OBJECT (data->volume_element),
        "volume", &v, NULL);

    *out_volume = v;
    return TRUE;
}

static gboolean
get_current_position (GstData *data, gdouble *out_position)
{
    GstFormat fmt = GST_FORMAT_TIME;
    gint64 timestamp;

    if (!gst_element_query_position (data->pipeline, &fmt, &timestamp)) {
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
set_controller_values (GstData *data, gdouble start_volume, gdouble end_time, gdouble end_volume)
{
    GValue v = {0,{{0}}};

    gst_controller_set_disabled (data->controller, TRUE);
    gst_interpolation_control_source_unset_all (data->csource);

    g_value_init (&v, G_TYPE_DOUBLE);
    g_value_set_double (&v, start_volume);
    gst_interpolation_control_source_set (data->csource,
        0, &v); /* start_time */

    g_value_reset (&v);
    g_value_set_double (&v, (end_volume));
    gst_interpolation_control_source_set (data->csource,
        end_time * GST_SECOND, &v);

    g_value_unset (&v);
    gst_controller_set_disabled (data->controller, FALSE);
}

static void
reset_linear_volume (GstData *data, gboolean query_position)
{
    gdouble timeleft, current_volume, position;

    if (!data->has_linear_volume)
        return;

    current_volume = data->linear_volume[0] / 100.0;
    timeleft = data->linear_volume[2];
    data->time_played = 0.0;

    if (query_position) {
        if (!get_current_position (data, &position))
            goto finish_controller;

        data->time_played += position;
        timeleft = data->linear_volume[2] - data->time_played;

        get_current_volume (data, &current_volume);
    }

    if (timeleft > 0.0) {
        N_DEBUG (LOG_CAT "query=%s, timeleft = %f, current_volume = %f",
            query_position ? "TRUE" : "FALSE", timeleft, current_volume);

        set_controller_values (data, current_volume,
            timeleft * GST_SECOND, data->linear_volume[1] / 100.0);

        return;
    }

finish_controller:
    if (data->controller) {
        N_DEBUG (LOG_CAT "controller finished");
        g_object_unref (data->controller);
        data->controller = NULL;
    }
}

static void
pipeline_rewind (GstElement *pipeline, gboolean flush)
{
    GstEvent *event;
    GstSeekFlags flags = GST_SEEK_FLAG_SEGMENT;

    if (flush)
        flags |= GST_SEEK_FLAG_FLUSH;

    event = gst_event_new_seek (1.0, GST_FORMAT_BYTES, flags, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_END, GST_CLOCK_TIME_NONE);
    if (!gst_element_send_event (pipeline, event)) {
        N_WARNING ("pipeline_rewind: failed to send event\n");
    }
}

static gboolean
bus_cb (GstBus *bus, GstMessage *msg, gpointer userdata)
{
    GstData *stream = (GstData*) userdata;

    (void) bus;

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR: {
            gst_element_set_state (stream->pipeline, GST_STATE_NULL);

            n_sink_interface_fail (stream->iface, stream->request);
            return FALSE;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

            if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {

                /* if the stream is not a repeating one, no need to send the initial flushing
                   segmented seek. instead we will wait for the eos to arrive. */

                if (stream->repeating) {
                    pipeline_rewind (stream->pipeline, TRUE);
                }

                n_sink_interface_synchronize (stream->iface, stream->request);
            }
            break;
        }

        case GST_MESSAGE_SEGMENT_DONE: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

            /* reset the linear volume controller values for the next iteration
               of the stream. if there is no linear volume, then just emit the
               rewind state change or completion. */

            reset_linear_volume (stream, TRUE);
            pipeline_rewind (stream->pipeline, FALSE);
            gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);

            n_sink_interface_resynchronize (stream->iface, stream->request);
            break;
        }

        case GST_MESSAGE_EOS: {
            if (GST_ELEMENT (GST_MESSAGE_SRC (msg)) != stream->pipeline)
                break;

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
gst_sink_initialize (NSinkInterface *iface)
{
    (void) iface;

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
    const NProplist *props = n_request_get_properties (request);

    if (n_proplist_has_key (props, "sound.filename")) {
        N_DEBUG (LOG_CAT "sink can_handle");
        return TRUE;
    }

    return FALSE;
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
convert_stream_property_cb (const char *key, const NValue *value,
                            gpointer userdata)
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
    if (set_structure_string (target, prop_key, prop_value)) {
        N_DEBUG (LOG_CAT "set stream property '%s' to '%s'",
            prop_key, prop_value);
    }
}

static GstStructure*
create_stream_properties (NProplist *props)
{
    g_assert (props != NULL);

    GstStructure *s      = gst_structure_empty_new ("props");
    const char   *source = NULL;

    /* set the stream filename based on the sound file we're
       about to play. */

    source = n_proplist_get_string (props, "sound.filename");
    g_assert (source != NULL);

    set_structure_string (s, "media.filename", source);

    /* set a media.role to "media" */

    set_structure_string (s, "media.role", "media");

    /* convert all properties within the request that begin with
       "sound.stream." prefix. */

    n_proplist_foreach (props, convert_stream_property_cb, s);

    return s;
}

static int
gst_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink prepare");

    GstElement   *source    = NULL;
    GstElement   *decodebin = NULL;
    GstElement   *sink      = NULL;
    GstBus       *bus       = NULL;

    const NProplist *props = n_request_get_properties (request);

    if (!n_proplist_has_key (props, "sound.filename"))
        return FALSE;

    GstData *data = g_slice_new0 (GstData);

    data->request   = request;
    data->iface     = iface;
    data->source    = n_proplist_get_string (props, "sound.filename");
    data->repeating = n_proplist_get_bool (props, "sound.repeat");

    n_request_store_data (request, GST_KEY, data);

    data->pipeline = gst_pipeline_new (NULL);
    parse_linear_volume (data, n_proplist_get_string (props, "sound.volume"));

    source    = gst_element_factory_make ("filesrc", NULL);
    decodebin = gst_element_factory_make ("decodebin2", NULL);
    sink      = gst_element_factory_make ("pulsesink", NULL);

    if (data->pipeline == NULL || source == NULL || decodebin == NULL || sink == NULL)
        goto failed;

    if (data->has_linear_volume) {
        if ((data->volume_element = gst_element_factory_make ("volume", NULL)) == NULL)
            goto failed;

        if ((data->controller = gst_controller_new (G_OBJECT (data->volume_element), "volume", NULL)) == NULL)
            goto failed;

        data->csource = gst_interpolation_control_source_new ();
        gst_controller_set_control_source (data->controller, "volume", GST_CONTROL_SOURCE (data->csource));
        gst_interpolation_control_source_set_interpolation_mode (data->csource, GST_INTERPOLATE_LINEAR);

        reset_linear_volume (data, FALSE);

        gst_bin_add_many (GST_BIN (data->pipeline), source, decodebin,
        data->volume_element, sink, NULL);

        if (!gst_element_link (data->volume_element, sink))
            goto failed_link;

        g_signal_connect (G_OBJECT (decodebin), "new-decoded-pad", G_CALLBACK (new_decoded_pad_cb), data->volume_element);
    } else {
        gst_bin_add_many (GST_BIN (data->pipeline), source, decodebin, sink, NULL);
        g_signal_connect (G_OBJECT (decodebin), "new-decoded-pad", G_CALLBACK (new_decoded_pad_cb), sink);
    }

    if (!gst_element_link (source, decodebin))
        goto failed_link;

    N_DEBUG (LOG_CAT "using source '%s'",data->source);
    g_object_set (G_OBJECT (source), "location", data->source, NULL);

    data->properties = create_stream_properties ((NProplist*) props);
    set_stream_properties (sink, data->properties);

    if (_gst_plugin_buffer_time > 0) {
        N_DEBUG (LOG_CAT "buffer-time set to %d", _gst_plugin_buffer_time);
        g_object_set (G_OBJECT (sink), "buffer-time", _gst_plugin_buffer_time, NULL);
    }

    if (_gst_plugin_latency_time > 0) {
        N_DEBUG (LOG_CAT "latency-time set to %d", _gst_plugin_latency_time);
        g_object_set (G_OBJECT (sink), "latency-time", _gst_plugin_latency_time, NULL);
    }

    bus = gst_element_get_bus (data->pipeline);
    gst_bus_add_watch (bus, bus_cb, data);
    gst_object_unref (bus);

    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);

    return TRUE;

failed:
    if (data->pipeline)
        gst_object_unref (data->pipeline);
    if (source)
        gst_object_unref (source);
    if (decodebin)
        gst_object_unref (decodebin);
    if (data->volume_element)
        gst_object_unref (data->volume_element);
    if (sink)
        gst_object_unref (sink);
    if (data->controller)
        gst_object_unref (data->controller);
    return FALSE;

failed_link:
    if (data->pipeline)
        gst_object_unref (data->pipeline);
    return FALSE;
}

static int
gst_sink_play (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink play");

    (void) iface;

    GstData *data = (GstData*) n_request_get_data (request, GST_KEY);
    g_assert (data != NULL);

    gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
    data->paused = FALSE;

    return TRUE;
}

static int
gst_sink_pause (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink pause");

    (void) iface;

    GstData *data = (GstData*) n_request_get_data (request, GST_KEY);
    g_assert (data != NULL);

    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
    data->paused = TRUE;

    return TRUE;
}

static void
gst_sink_stop (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink stop");

    (void) iface;

    GstData *data = (GstData*) n_request_get_data (request, GST_KEY);
    g_assert (data != NULL);

    if (data->pipeline) {
        gst_element_set_state (data->pipeline, GST_STATE_NULL);
        gst_object_unref (data->pipeline);
        data->pipeline = NULL;
    }

    if (data->controller) {
        gst_interpolation_control_source_unset_all (data->csource);
        g_object_unref (data->controller);
        data->controller = NULL;
    }

    if (data->properties) {
        gst_structure_free (data->properties);
        data->properties = NULL;
    }

    g_slice_free (GstData, data);
}

N_PLUGIN_LOAD (plugin)
{
    NProplist   *params = NULL;
    const char  *value  = NULL;

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

    /* parse the buffer and latency times, if available */

    params = (NProplist*) n_plugin_get_params (plugin);

    if ((value = n_proplist_get_string (params, "buffer-time")))
        _gst_plugin_buffer_time = atoi (value);

    if ((value = n_proplist_get_string (params, "latency-time")))
        _gst_plugin_latency_time = atoi (value);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;
}
