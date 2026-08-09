
#include "wavetable.h"
#include "accordion_tables.h"

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif


/* ==========================================================================
 * Wavetables
 * ========================================================================== */

int16_t wavetable_sine[WAVETABLE_SIZE];
int16_t wavetable_saw[WAVETABLE_SIZE];

int16_t wavetable_accordion_low[WAVETABLE_SIZE];
int16_t wavetable_accordion_mid[WAVETABLE_SIZE];
int16_t wavetable_accordion_high[WAVETABLE_SIZE];


/* ==========================================================================
 * LoadWavetable
 *
 * Convertit une période complète provenant d'une table source vers une
 * wavetable de WAVETABLE_SIZE points.
 *
 * Étapes :
 *   1. suppression de l'offset DC
 *   2. normalisation
 *   3. rééchantillonnage avec interpolation linéaire
 *   4. interpolation cyclique : le dernier point rejoint le premier
 *
 * IMPORTANT :
 * source doit contenir UNE SEULE période complète.
 *
 * Exemple :
 *
 *     la3  : ~190 points -> 256 points
 *     la4  : ~100 points -> 256 points
 *     la5  : ~50 points  -> 256 points
 *
 * ========================================================================== */

static void LoadWavetable(
    const int16_t *source,
    int sourceSize,
    int16_t *destination)
{
    if(source == NULL ||
       destination == NULL ||
       sourceSize <= 0)
    {
        return;
    }


    /* ----------------------------------------------------------------------
     * 1. Calcul de l'offset DC
     * ---------------------------------------------------------------------- */

    float offset = 0.0f;

    for(int i = 0; i < sourceSize; i++)
    {
        offset += (float)source[i];
    }

    offset /= (float)sourceSize;


    /* ----------------------------------------------------------------------
     * 2. Recherche de l'amplitude maximale après suppression du DC
     * ---------------------------------------------------------------------- */

    float maxAbs = 0.0f;

    for(int i = 0; i < sourceSize; i++)
    {
        float value =
            (float)source[i] - offset;

        float absValue =
            fabsf(value);

        if(absValue > maxAbs)
        {
            maxAbs = absValue;
        }
    }


    /* Protection contre une division par zéro */

    if(maxAbs < 1.0f)
    {
        maxAbs = 1.0f;
    }


    /* ----------------------------------------------------------------------
     * 3. Normalisation
     *
     * On garde une marge sous le maximum int16.
     * Cela évite de travailler constamment à ±32767.
     * ---------------------------------------------------------------------- */

    const float TARGET_PEAK = 30000.0f;

    float gain =
        TARGET_PEAK / maxAbs;


    /* ----------------------------------------------------------------------
     * 4. Rééchantillonnage cyclique
     *
     * Pour chaque point de sortie :
     *
     *     position = i * sourceSize / WAVETABLE_SIZE
     *
     * On interpole entre :
     *
     *     source[index]
     *     source[index + 1]
     *
     * avec retour à zéro après le dernier échantillon.
     *
     * Cela est important pour une wavetable périodique.
     * ---------------------------------------------------------------------- */

    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {
        float position =
            ((float)i * (float)sourceSize)
            / (float)WAVETABLE_SIZE;


        int index =
            (int)position;


        float fraction =
            position - (float)index;


        /* Sécurité */

        if(index >= sourceSize)
        {
            index = sourceSize - 1;
            fraction = 0.0f;
        }


        /* Interpolation cyclique */

        int nextIndex =
            index + 1;

        if(nextIndex >= sourceSize)
        {
            nextIndex = 0;
        }


        float s1 =
            (float)source[index] - offset;

        float s2 =
            (float)source[nextIndex] - offset;


        float interpolated =
            s1 + (s2 - s1) * fraction;


        float sample =
            interpolated * gain;


        /* Saturation de sécurité */

        if(sample > 32767.0f)
        {
            sample = 32767.0f;
        }

        if(sample < -32768.0f)
        {
            sample = -32768.0f;
        }


        destination[i] =
            (int16_t)sample;
    }
}


/* ==========================================================================
 * Wavetable_Init
 * ========================================================================== */

void Wavetable_Init(void)
{
    /* ----------------------------------------------------------------------
     * Sinus
     * ---------------------------------------------------------------------- */

    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {
        float phase =
            2.0f *
            (float)M_PI *
            (float)i /
            (float)WAVETABLE_SIZE;


        wavetable_sine[i] =
            (int16_t)
            (32767.0f * sinf(phase));
    }


    /* ----------------------------------------------------------------------
     * Saw
     *
     * Une période complète sur WAVETABLE_SIZE points.
     * ---------------------------------------------------------------------- */

    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {
        wavetable_saw[i] =
            (int16_t)
            (
                -32768.0f +
                65535.0f *
                (float)i /
                (float)(WAVETABLE_SIZE - 1)
            );
    }


    /* ----------------------------------------------------------------------
     * Accordéon réel
     *
     * Les tables sources contiennent chacune une période :
     *
     *     la3 : environ 190 échantillons
     *     la4 : environ 100 échantillons
     *     la5 : environ  50 échantillons
     *
     * Elles sont toutes converties vers 256 points.
     * ---------------------------------------------------------------------- */

    LoadWavetable(
        la3,
        LA3_SIZE,
        wavetable_accordion_low
    );


    LoadWavetable(
        la4,
        LA4_SIZE,
        wavetable_accordion_mid
    );


    LoadWavetable(
        la5,
        LA5_SIZE,
        wavetable_accordion_high
    );
}

