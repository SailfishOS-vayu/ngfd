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

#include <ngf/plugin.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gio/gio.h>

#include <pulse/proplist.h>

#include "volume.h"

#define GST_KEY "plugin.gst.data"
#define LOG_CAT  "gst: "

typedef struct _GstData
{
    NRequest       *request;
    NSinkInterface *iface;
    GstElement              *pipeline;
    Volume                  *volume;
    GstElement              *volume_element;
    GstController           *controller;
    GstInterpolationControlSource *csource;
    const gchar                  *source;
    GstStructure           *properties;
    gdouble                 time_played;
    gint                    buffer_time;
    gint                    latency_time;
    gboolean                paused;
    guint                   current_repeat;
    guint                   num_repeats;
    gboolean                repeating;
} GstData;

N_PLUGIN_NAME        ("gst")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_AUTHOR      ("Harri Mahonen <ext-harri.mahonen@nokia.com>")
N_PLUGIN_DESCRIPTION ("Gdataer plugin")

static void
reset_linear_volume (GstData *data, gboolean query_position)
{
    Volume    *volume = data->volume;
    GstFormat  fmt    = GST_FORMAT_TIME;
    GValue     v = {0,{{0}}};
    gint64  timestamp;
    gdouble timeleft, current_volume;

    if (!volume || volume->type != VOLUME_TYPE_LINEAR)
        return;

    if (query_position) {
        if (!gst_element_query_position (data->pipeline, &fmt, &timestamp)) {
            N_DEBUG ("%s >> unable to query data position",
                __FUNCTION__);
            goto finish_controller;
        }

        if (!(GST_CLOCK_TIME_IS_VALID (timestamp) && fmt == GST_FORMAT_TIME)) {
            N_DEBUG ("%s >> queried position or format is not valid",
                __FUNCTION__);
            goto finish_controller;
        }

        data->time_played += (gdouble) timestamp / GST_SECOND;
        timeleft = data->volume->linear[2] - data->time_played;

        g_object_get (G_OBJECT (data->volume_element),
            "volume", &current_volume, NULL);
    }
    else {
        data->time_played = 0.0;
        timeleft            = data->volume->linear[2];
        current_volume      = data->volume->linear[0] / 100.0;
    }

    if (timeleft > 0.0) {
        N_DEBUG ("%s >> query=%s, timeleft = %f, current_volume = %f\n",
            __FUNCTION__, query_position ? "TRUE" : "FALSE", timeleft,
            current_volume);

        gst_controller_set_disabled (data->controller, TRUE);
        gst_interpolation_control_source_unset_all (data->csource);

        g_value_init (&v, G_TYPE_DOUBLE);
        g_value_set_double (&v, current_volume);
        gst_interpolation_control_source_set (data->csource,
            0 * GST_SECOND, &v);

        g_value_reset (&v);
        g_value_set_double (&v, (data->volume->linear[1] / 100.0));
        gst_interpolation_control_source_set (data->csource,
            timeleft * GST_SECOND, &v);

        g_value_unset (&v);
        gst_controller_set_disabled (data->controller, FALSE);

        return;
    }

finish_controller:
    if (data->controller) {
        N_DEBUG ("%s >> controller finished\n", __FUNCTION__);
        g_object_unref (data->controller);
        data->controller = NULL;
    }
}

static void
gst_element_preload (gchar * name)
{
	GstElement *element = NULL;

	if ((element = gst_element_factory_make (name, NULL)) == NULL) {
		N_WARNING ("Preloading element %s failed", name);
		return;
	}

	g_object_unref (element);
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
bus_cb (GstBus     *bus,
         GstMessage *msg,
         gpointer    userdata)
{
    GstData *stream = (GstData*) userdata;
    (void) bus;

    N_DEBUG (LOG_CAT "Entering bus_cb");

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

            else if (old_state == GST_STATE_PAUSED && new_state == GST_STATE_PLAYING) {
                if (stream->paused)
                    break;

                stream->current_repeat++;
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

            if (stream->num_repeats == 0 || (stream->current_repeat > stream->num_repeats)) {
                pipeline_rewind (stream->pipeline, FALSE);
                gst_element_set_state (stream->pipeline, GST_STATE_PAUSED);

                /*
                if (stream->callback)
                    stream->callback (stream, AUDIO_STREAM_STATE_REWIND,
                        stream->userdata);
                */
            }
            else {
                n_sink_interface_complete (stream->iface, stream->request);

                return FALSE;
            }

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

static gboolean
structure_to_proplist_cb (GQuark field_id, const GValue *value, gpointer userdata)
{
    pa_proplist *proplist = (pa_proplist*) userdata;

    if (G_VALUE_HOLDS_STRING (value))
        pa_proplist_sets (proplist, g_quark_to_string (field_id), g_value_get_string (value));

    return TRUE;
}

static void
new_decoded_pad_cb (GstElement *element,
                     GstPad     *pad,
                     gboolean    is_last,
                     gpointer    userdata)
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
    pa_proplist *proplist = NULL;

    if (!sink | !properties)
        return;

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "stream-properties") != NULL) {
        g_object_set (G_OBJECT (sink), "stream-properties", properties, NULL);
    }

    else if (g_object_class_find_property (G_OBJECT_GET_CLASS (sink), "proplist") != NULL) {
        proplist = pa_proplist_new ();
        gst_structure_foreach (properties, structure_to_proplist_cb, proplist);
        g_object_set (G_OBJECT (sink), "proplist", proplist, NULL);

        /* no need ot unref proplist, ownership is taken by the sink */
    }
}

static int
gst_sink_initialize (NSinkInterface *iface)
{
    (void) iface;

    gst_init_check (NULL, NULL, NULL);
    gst_controller_init (NULL, NULL);

    gst_element_preload ("aacparse");
    gst_element_preload ("nokiaaacdec");
    gst_element_preload ("id3demux");
    gst_element_preload ("uridecodebin");
    gst_element_preload ("mp3parse");
    gst_element_preload ("nokiamp3dec");
    gst_element_preload ("wavparse");
    gst_element_preload ("oggdemux");
    gst_element_preload ("ivorbisdec");
    gst_element_preload ("filesrc");
    gst_element_preload ("decodebin2");
    gst_element_preload ("volume");
    gst_element_preload ("pulsesink");
    
    return TRUE;
}

static void
gst_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
    N_DEBUG (LOG_CAT "sink shutdown");
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
gst_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    N_DEBUG (LOG_CAT "sink prepare");

    GstElement   *source    = NULL;
    GstElement   *decodebin = NULL;
    GstElement   *sink      = NULL;
    GstBus       *bus       = NULL;
    GValue        v = {0,{{0}}};
    const char *value = NULL;

    const NProplist *props = n_request_get_properties (request);

    if (!n_proplist_has_key (props, "sound.filename"))
        return FALSE;

    GstData *data = g_slice_new0 (GstData);

    data->request    = request;
    data->iface      = iface;
    data->source = n_proplist_get_string (props, "sound.filename");
    data->repeating = n_proplist_get_bool (props, "sound.repeat");
    data->properties = gst_structure_empty_new ("props");

    n_request_store_data (request, GST_KEY, data);

    data->pipeline = gst_pipeline_new (NULL);
    data->volume = create_volume (n_proplist_get_string (props, "sound.volume"));

    source    = gst_element_factory_make ("filesrc", NULL);
    decodebin = gst_element_factory_make ("decodebin2", NULL);
    sink      = gst_element_factory_make ("pulsesink", NULL);

    if (data->pipeline == NULL || source == NULL || decodebin == NULL || sink == NULL)
        goto failed;

    if (data->volume && data->volume->type == VOLUME_TYPE_LINEAR) {
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

    N_DEBUG (LOG_CAT "Source is %s",data->source);
    g_object_set (G_OBJECT (source), "location", data->source, NULL);

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, data->source);
    gst_structure_set_value (data->properties, "media.filename", &v);
    g_value_unset (&v);

    value = n_proplist_get_string (props, "sound.event_id");
    if (value) {
        N_DEBUG ("%s >> set stream event id to %s", __FUNCTION__, value);
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, value);
        gst_structure_set_value (data->properties, "event.id", &v);
        g_value_unset (&v);
    }

    value = n_proplist_get_string (props, "sound.stream-restore-id") ? n_proplist_get_string (props, "sound.stream-restore-id") : "x-maemo";
    N_DEBUG ("%s >> set stream role to %s", __FUNCTION__, value);
    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    gst_structure_set_value (data->properties, "module-stream-restore.id", &v);
    g_value_unset (&v);

    set_stream_properties (sink, data->properties);

    if (data->buffer_time > 0)
        g_object_set (G_OBJECT (sink), "buffer-time", data->buffer_time, NULL);

    if (data->latency_time > 0)
        g_object_set (G_OBJECT (sink), "latency-time", data->latency_time, NULL);

    bus = gst_element_get_bus (data->pipeline);
    gst_bus_add_watch (bus, bus_cb, data);
    gst_object_unref (bus);

    gst_element_set_state (data->pipeline, GST_STATE_PAUSED);

    return TRUE;

failed:
    if (data->pipeline)   gst_object_unref (data->pipeline);
    if (source)    gst_object_unref (source);
    if (decodebin) gst_object_unref (decodebin);
    if (data->volume_element)     gst_object_unref (data->volume_element);
    if (sink)      gst_object_unref (sink);
    if (data->controller) gst_object_unref (data->controller);
    return FALSE;

failed_link:
    if (data->pipeline)   gst_object_unref (data->pipeline);
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
    N_DEBUG (LOG_CAT "plugin load");

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

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;

    N_DEBUG (LOG_CAT "plugin unload");
}
