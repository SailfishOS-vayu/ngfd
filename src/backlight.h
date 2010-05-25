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

#ifndef BACKLIGHT_H
#define BACKLIGHT_H

#include <glib.h>

typedef struct _Backlight Backlight;

Backlight*      backlight_create   ();
void            backlight_destroy  (Backlight *self);
gboolean        backlight_start    (Backlight *self, gboolean unlock);
void            backlight_stop     (Backlight *self);

#endif /* BACKLIGHT_H */
