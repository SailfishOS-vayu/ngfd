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

#ifndef N_PROPLIST_H
#define N_PROPLIST_H

typedef struct _NProplist NProplist;

#include <ngf/value.h>

typedef void (*NProplistFunc) (const char *key, const NValue *value, gpointer userdata);

NProplist*  n_proplist_new         ();
NProplist*  n_proplist_copy        (const NProplist *source);
NProplist*  n_proplist_copy_keys   (const NProplist *source, GList *keys);
void        n_proplist_merge       (NProplist *target, const NProplist *source);
void        n_proplist_merge_keys  (NProplist *target, const NProplist *source, GList *keys);
void        n_proplist_free        (NProplist *proplist);

int         n_proplist_size        (const NProplist *proplist);
void        n_proplist_foreach     (const NProplist *proplist, NProplistFunc func, gpointer userdata);
gboolean    n_proplist_is_empty    (const NProplist *proplist);
gboolean    n_proplist_has_key     (const NProplist *proplist, const char *key);
gboolean    n_proplist_match_exact (const NProplist *a, const NProplist *b);

void        n_proplist_set         (NProplist *proplist, const char *key, const NValue *value);
NValue*     n_proplist_get         (const NProplist *proplist, const char *key);

/* helpers */

void        n_proplist_unset       (NProplist *proplist, const char *key);
void        n_proplist_set_string  (NProplist *proplist, const char *key, const char *value);
const char* n_proplist_get_string  (const NProplist *proplist, const char *key);
gchar*      n_proplist_dup_string  (const NProplist *proplist, const char *key);
void        n_proplist_set_int     (NProplist *proplist, const char *key, gint value);
gint        n_proplist_get_int     (const NProplist *proplist, const char *key);
void        n_proplist_set_uint    (NProplist *proplist, const char *key, guint value);
guint       n_proplist_get_uint    (const NProplist *proplist, const char *key);
void        n_proplist_set_bool    (NProplist *proplist, const char *key, gboolean value);
gboolean    n_proplist_get_bool    (const NProplist *proplist, const char *key);
void        n_proplist_set_pointer (NProplist *proplist, const char *key, gpointer value);
gpointer    n_proplist_get_pointer (const NProplist *proplist, const char *key);
void        n_proplist_dump        (const NProplist *proplist);

#endif /* N_PROPLIST_H */
