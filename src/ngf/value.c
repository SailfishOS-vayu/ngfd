#include <string.h>
#include "value.h"

struct _NValue
{
    guint type;
    union {
        gchar   *s;
        gint     i;
        guint    u;
        gboolean b;
        gpointer p;
    } value;
};

NValue*
n_value_new ()
{
    return (NValue*) g_slice_new0 (NValue);
}

void
n_value_free (NValue *value)
{
    if (!value)
        return;

    n_value_clean (value);
    g_slice_free (NValue, value);
}

void
n_value_init (NValue *value)
{
    if (!value)
        return;

    memset (value, 0, sizeof (NValue));
}

void
n_value_clean (NValue *value)
{
    if (!value)
        return;

    if (value->type == N_VALUE_TYPE_STRING) {
        g_free (value->value.s);
        value->value.s = NULL;
    }
}

NValue*
n_value_copy (NValue *value)
{
    NValue *new_value = NULL;

    if (!value)
        return NULL;

    if (value->type == 0)
        return NULL;

    new_value = n_value_new ();
    new_value->type = value->type;

    switch (value->type) {
        case N_VALUE_TYPE_STRING:
            new_value->value.s = g_strdup (value->value.s);
            break;
        case N_VALUE_TYPE_INT:
            new_value->value.i = value->value.i;
            break;
        case N_VALUE_TYPE_UINT:
            new_value->value.u = value->value.u;
            break;
        case N_VALUE_TYPE_BOOL:
            new_value->value.b = value->value.b;
            break;
        case N_VALUE_TYPE_POINTER:
            new_value->value.p = value->value.p;
            break;
        default:
            n_value_free (new_value);
            return NULL;
    }

    return new_value;
}

int
n_value_type (NValue *value)
{
    if (!value)
        return 0;

    return value->type;
}

void
n_value_set_string (NValue *value, const char *in_value)
{
    if (!value || !in_value)
        return;

    value->type    = N_VALUE_TYPE_STRING;
    value->value.s = g_strdup (in_value);
}

const gchar*
n_value_get_string (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_STRING) ? (const char*) value->value.s : NULL;
}

gchar*
n_value_dup_string (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_STRING) ? g_strdup (value->value.s) : NULL;
}

void
n_value_set_int (NValue *value, gint in_value)
{
    if (!value)
        return;

    value->type    = N_VALUE_TYPE_INT;
    value->value.i = in_value;
}

gint
n_value_get_int (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_INT) ? value->value.i : 0;
}

void
n_value_set_uint (NValue *value, guint in_value)
{
    if (!value || !in_value)
        return;

    value->type    = N_VALUE_TYPE_UINT;
    value->value.u = in_value;
}

guint
n_value_get_uint (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_UINT) ? value->value.u : 0;
}

void
n_value_set_bool (NValue *value, gboolean in_value)
{
    if (!value)
        return;

    value->type    = N_VALUE_TYPE_BOOL;
    value->value.b = in_value;
}

gboolean
n_value_get_bool (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_BOOL) ? value->value.b : FALSE;
}

void
n_value_set_pointer (NValue *value, gpointer in_value)
{
    if (!value)
        return;

    value->type    = N_VALUE_TYPE_POINTER;
    value->value.p = in_value;
}

gpointer
n_value_get_pointer (NValue *value)
{
    return (value && value->type == N_VALUE_TYPE_POINTER) ? value->value.p : NULL;
}

gchar*
n_value_to_string (NValue *value)
{
    gchar *result = NULL;

    if (!value)
        return g_strdup_printf ("<null>");

    switch (value->type) {
        case N_VALUE_TYPE_STRING:
            result = g_strdup_printf ("%s (string)", value->value.s);
            break;

        case N_VALUE_TYPE_INT:
            result = g_strdup_printf ("%d (int)", value->value.i);
            break;

        case N_VALUE_TYPE_UINT:
            result = g_strdup_printf ("%u (uint)", value->value.u);
            break;

        case N_VALUE_TYPE_BOOL:
            result = value->value.b ? g_strdup ("TRUE (bool)") : g_strdup ("FALSE (bool)");
            break;

        case N_VALUE_TYPE_POINTER:
            result = g_strdup_printf ("0x%X (pointer)", (unsigned int) value->value.p);
            break;

        default:
            result = g_strdup ("<unknown value>");
            break;
    }

    return result;
}

