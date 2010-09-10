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

#include "audio-interface.h"

gboolean
audio_interface_initialize (AudioInterface *iface)
{
    return iface->initialize (iface);
}

void
audio_interface_shutdown (AudioInterface *iface)
{
    iface->shutdown (iface);
}

AudioStream*
audio_interface_create_stream (AudioInterface *iface)
{
    AudioStream *stream = NULL;

    stream = g_slice_new0 (AudioStream);
    stream->iface = (gpointer) iface;
    return stream;
}

void
audio_interface_destroy_stream (AudioInterface *iface, AudioStream *stream)
{
    if (iface == NULL || stream == NULL)
        return;

    if (stream->properties) {
        gst_structure_free (stream->properties);
        stream->properties = NULL;
    }

    g_free (stream->source);
    g_slice_free (AudioStream, stream);
}

gboolean
audio_interface_prepare (AudioInterface *iface, AudioStream *stream)
{
    return iface->prepare (iface, stream);
}

gboolean
audio_interface_play (AudioInterface *iface, AudioStream *stream)
{
    return iface->play (iface, stream);
}

void
audio_interface_stop (AudioInterface *iface, AudioStream *stream)
{
    iface->stop (iface, stream);
}
