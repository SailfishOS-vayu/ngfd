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

#ifndef N_EVENT_H
#define N_EVENT_H

/** Internal event structure. */
typedef struct _NEvent NEvent;

#include <ngf/proplist.h>

/**
 * Get event name
 *
 * @param event Event structure.
 * @return Name of the event.
 */
const char*      n_event_get_name       (NEvent *event);

/**
 * Get properties associated to event
 *
 * @param event Event structure.
 * @return Property list.
 */
const NProplist* n_event_get_properties (NEvent *event);

#endif /* N_EVENT_H */
