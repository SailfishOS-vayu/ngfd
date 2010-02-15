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

#ifndef NGF_LED_H
#define NGF_LED_H

#include <glib.h>

typedef struct _NgfLed NgfLed;

NgfLed*         ngf_led_create ();
void            ngf_led_destroy (NgfLed *self);

guint           ngf_led_start (NgfLed *self, const gchar *pattern);
void            ngf_led_stop (NgfLed *self, guint id);

#endif /* NGF_LED_H */
