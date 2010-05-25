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

#include <glib.h>

#include "log.h"
#include "context.h"

static gboolean _request_manager_create         (Context *context);
static void     _request_manager_destroy        (Context *context);

static DBusConnection*
_get_dbus_connection (DBusBusType bus_type)
{
    DBusConnection *bus = NULL;
    DBusError       error;

    dbus_error_init (&error);
    bus = dbus_bus_get (bus_type, &error);
    if (bus == NULL) {
        LOG_WARNING ("Failed to get %s bus: %s", bus_type == DBUS_BUS_SYSTEM ? "system" : "session", error.message);
        dbus_error_free (&error);
        return NULL;
    }

    return bus;
}

int
context_create (Context **context)
{
    Context *c = NULL;

    if ((c = g_try_malloc0 (sizeof (Context))) == NULL) {
        LOG_ERROR ("Failed to allocate memory for context!");
        return FALSE;
    }

    if ((c->loop = g_main_loop_new (NULL, 0)) == NULL) {
        LOG_ERROR ("Failed to create the GLib mainloop!");
        return FALSE;
    }

    /* setup the dbus connections. we will hook up to the session bus, but use
       the system bus too for led, backlight and tone generator. */

    c->system_bus  = _get_dbus_connection (DBUS_BUS_SYSTEM);
    c->session_bus = _get_dbus_connection (DBUS_BUS_SESSION);

    if (!c->system_bus || !c->session_bus) {
        LOG_ERROR ("Failed to get system/session bus!");
        return FALSE;
    }

    dbus_connection_setup_with_g_main (c->system_bus, NULL);
    dbus_connection_setup_with_g_main (c->session_bus, NULL);

    /* setup the interface */

    if (!dbus_if_create (c)) {
        LOG_ERROR ("Failed to create D-Bus interface!");
        return FALSE;
    }

    /* setup the backends */

    if ((c->profile = profile_create ()) == NULL) {
        LOG_ERROR ("Failed to create profile tracking!");
        return FALSE;
    }

    if ((c->tone_mapper = tone_mapper_create ()) == NULL) {
        LOG_WARNING ("Failed to create tone mapper!");
    }

    if ((c->audio = audio_create ()) == NULL) {
        LOG_ERROR ("Failed to create Pulseaudio backend!");
        return FALSE;
    }

    if ((c->vibrator = vibrator_create ()) == NULL) {
        LOG_ERROR ("Failed to create Immersion backend!");
        return FALSE;
    }

    if ((c->backlight = backlight_create ()) == NULL) {
        LOG_ERROR ("Failed to create backlight backend!");
        return FALSE;
    }

    /* create the hash tables to hold definitions and events */

    if (!_request_manager_create (c)) {
        LOG_ERROR ("Failed to create request manager!");
        return FALSE;
    }

    /* load settings */

    if (!load_settings (c)) {
        LOG_ERROR ("Failed to load settings!");
        return FALSE;
    }

    *context = c;
    return TRUE;
}

void
context_destroy (Context *context)
{
    dbus_if_destroy (context);

    if (context->session_bus) {
        dbus_connection_unref (context->session_bus);
        context->session_bus = NULL;
    }

    if (context->system_bus) {
        dbus_connection_unref (context->system_bus);
        context->system_bus = NULL;
    }

    if (context->backlight) {
        backlight_destroy (context->backlight);
	    context->backlight = NULL;
    }

    if (context->vibrator) {
        vibrator_destroy (context->vibrator);
        context->vibrator = NULL;
    }

    if (context->audio) {
        audio_destroy (context->audio);
        context->audio = NULL;
    }

    if (context->tone_mapper) {
        tone_mapper_destroy (context->tone_mapper);
        context->tone_mapper = NULL;
    }

    if (context->profile) {
        profile_destroy (context->profile);
        context->profile = NULL;
    }

    _request_manager_destroy (context);

    if (context->loop) {
        g_main_loop_unref (context->loop);
        context->loop = NULL;
    }

    g_free (context);
}

static gboolean
_request_manager_create (Context *context)
{
    context->definitions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) definition_free);
    if (context->definitions == NULL)
        return FALSE;

    context->events = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) event_free);
    if (context->events == NULL)
        return FALSE;

    return TRUE;
}

static void
_request_manager_destroy (Context *context)
{
    if (context->events) {
        g_hash_table_destroy (context->events);
        context->events = NULL;
    }

    if (context->definitions) {
        g_hash_table_destroy (context->definitions);
        context->definitions = NULL;
    }
}

int
main (int argc, char *argv[])
{
    Context *context = NULL;

    if (!context_create (&context))
        return 1;

    g_main_loop_run (context->loop);
    context_destroy (context);

    return 0;
}
