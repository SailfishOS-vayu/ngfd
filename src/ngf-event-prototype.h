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

#ifndef NGF_EVENT_PROTOTYPE_H
#define NGF_EVENT_PROTOTYPE_H

#include <glib.h>

typedef struct _NgfEventPrototype NgfEventPrototype;

struct _NgfEventPrototype
{
    gboolean    event_repeat;
    gint        event_max_length;

    gchar       *audio_filename;
    gchar       *audio_fallback;

    gchar       *audio_tone_key;
    gchar       *audio_fallback_key;
    gchar       *audio_volume_key;
    gchar       *audio_stream_restore;
};

NgfEventPrototype*  ngf_event_prototype_new ();
void                ngf_event_prototype_free (NgfEventPrototype *proto);

#endif /* NGF_EVENT_PROTOTYPE_H */
