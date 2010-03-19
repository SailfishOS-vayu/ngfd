#include "ngf-audio-interface.h"

gboolean
ngf_audio_interface_initialize (NgfAudioInterface *iface, NgfPulseContext *context)
{
    return iface->initialize (iface, context);
}

void
ngf_audio_interface_shutdown (NgfAudioInterface *iface)
{
    iface->shutdown (iface);
}

NgfAudioStream*
ngf_audio_interface_create_stream (NgfAudioInterface *iface)
{
    NgfAudioStream *stream = NULL;

    stream = g_slice_new0 (NgfAudioStream);
    stream->iface = (gpointer) iface;
    return stream;
}

void
ngf_audio_interface_destroy_stream (NgfAudioInterface *iface, NgfAudioStream *stream)
{
    if (iface == NULL || stream == NULL)
        return;

    if (stream->properties) {
        pa_proplist_free (stream->properties);
        stream->properties = NULL;
    }

    g_free (stream->source);
    g_slice_free (NgfAudioStream, stream);
}

gboolean
ngf_audio_interface_prepare (NgfAudioInterface *iface, NgfAudioStream *stream)
{
    return iface->prepare (iface, stream);
}

gboolean
ngf_audio_interface_play (NgfAudioInterface *iface, NgfAudioStream *stream)
{
    return iface->play (iface, stream);
}

void
ngf_audio_interface_stop (NgfAudioInterface *iface, NgfAudioStream *stream)
{
    iface->stop (iface, stream);
}
