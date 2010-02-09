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

#include "ngf-event-prototype.h"

NgfEventPrototype*
ngf_event_prototype_new ()
{
    NgfEventPrototype *proto = NULL;

    if ((proto = g_new0 (NgfEventPrototype, 1)) == NULL)
        return NULL;

    return proto;
}

void
ngf_event_prototype_free (NgfEventPrototype *proto)
{
    if (proto == NULL)
        return;

    g_free (proto->tone_filename);
    g_free (proto->tone_key);
    g_free (proto->tone_profile);

    g_free (proto->volume_key);
    g_free (proto->volume_profile);

    g_free (proto);
}

void
ngf_event_prototype_dump (NgfEventPrototype *proto)
{
    g_print ("max_length = %d\n", proto->max_length);
    g_print ("tone_filename = %s\n", proto->tone_filename);
    g_print ("tone_key = %s\n", proto->tone_key);
    g_print ("tone_profile = %s\n", proto->tone_profile);
    g_print ("volume_key = %s\n", proto->volume_key);
    g_print ("volume_profile = %s\n", proto->volume_profile);
}

