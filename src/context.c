#include "log.h"
#include "context.h"

SoundPath*
context_add_sound_path (Context *context, SoundPath *sound_path)
{
    SoundPath **i = NULL;

    if (sound_path == NULL)
        return NULL;

    if (context->sounds == NULL) {
        context->num_sounds = 1;
        context->sounds     = g_try_malloc0 (sizeof (SoundPath) * (context->num_sounds + 1));
        context->sounds[0]  = sound_path;
        return sound_path;
    }

    /* if we have already an entry for the sound path, return that. */

    for (i = context->sounds; *i; ++i) {
        if (sound_path_equals (*i, sound_path)) {
            sound_path_free (sound_path);
            return *i;
        }
    }

    /* we have a new sound path, add that. */

    context->num_sounds++;
    context->sounds = g_try_realloc (context->sounds, sizeof (SoundPath) * (context->num_sounds + 1));

    context->sounds[context->num_sounds-1] = sound_path;
    context->sounds[context->num_sounds]   = NULL;

    return sound_path;
}

VibrationPattern*
context_add_pattern (Context *context, VibrationPattern *pattern)
{
    VibrationPattern **i = NULL;

    if (pattern == NULL)
        return NULL;

    if (context->patterns == NULL) {
        context->num_patterns = 1;
        context->patterns     = g_try_malloc0 (sizeof (VibrationPattern) * (context->num_patterns + 1));
        context->patterns[0]  = pattern;
        return pattern;
    }

    for (i = context->patterns; *i; ++i) {
        if (vibration_pattern_equals (*i, pattern)) {
            vibration_pattern_free (pattern);
            return *i;
        }
    }

    if (pattern->type == VIBRATION_PATTERN_TYPE_FILENAME) {
        if ((pattern->data = vibrator_load (pattern->filename)) == NULL) {
            vibration_pattern_free (pattern);
            return NULL;
        }
    }

    context->num_patterns++;
    context->patterns = g_try_realloc (context->patterns, sizeof (VibrationPattern) * (context->num_patterns + 1));

    context->patterns[context->num_patterns-1] = pattern;
    context->patterns[context->num_patterns]   = NULL;

    return pattern;
}

Volume*
context_add_volume (Context *context, Volume *volume)
{
    Volume **i = NULL;

    if (volume == NULL)
        return NULL;

    if (context->volumes == NULL) {
        context->num_volumes = 1;
        context->volumes     = g_try_malloc0 (sizeof (Volume) * (context->num_volumes + 1));
        context->volumes[0]  = volume;
        return volume;
    }

    for (i = context->volumes; *i; ++i) {
        if (volume_equals (*i, volume)) {
            volume_free (volume);
            return *i;
        }
    }

    if (!volume_generate_role (volume)) {
        LOG_WARNING ("%s >> failed to generate role for volume!", __FUNCTION__);
        volume_free (volume);
        return NULL;
    }

    context->num_volumes++;
    context->volumes = g_try_realloc (context->volumes, sizeof (Volume) * (context->num_volumes + 1));

    context->volumes[context->num_volumes-1] = volume;
    context->volumes[context->num_volumes]   = NULL;

    return volume;
}
