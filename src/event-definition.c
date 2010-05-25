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

#include "event-definition.h"

EventDefinition*
event_definition_new ()
{
    return (EventDefinition*) g_new0 (EventDefinition, 1);
}

void
event_definition_free (EventDefinition *self)
{
    if (self == NULL)
        return;

    g_free (self->long_proto);
    g_free (self->short_proto);
    g_free (self->meeting_proto);
    g_free (self);
}
