
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

/* [position_musicale : 0=do3..5=sol5][niveau_BL : 0=plein..3=sinus][echantillon] */
int16_t wavetable_accordion[ACCORDION_NUM_WAVES][ACCORDION_NUM_BL][WAVETABLE_SIZE];

/* ==========================================================================
 * FFT radix-2 Cooley-Tukey (float, sur place)
 * Buffers statiques : 2 x 512 x 4 = 4 Ko — utilises uniquement a Wavetable_Init.
 * ========================================================================== */

static float fft_re[WAVETABLE_SIZE];
static float fft_im[WAVETABLE_SIZE];

static void bit_reverse_shuffle(float *re, float *im, int n)
{
    int j = 0;
    for(int i = 1; i < n; i++)
    {
        int bit = n >> 1;
        for(; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if(i < j)
        {
            float tmp;
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }
}

static void fft_forward(float *re, float *im, int n)
{
    bit_reverse_shuffle(re, im, n);
    for(int len = 2; len <= n; len <<= 1)
    {
        float ang     = -2.0f * (float)M_PI / (float)len;
        float wr_step = cosf(ang);
        float wi_step = sinf(ang);
        for(int i = 0; i < n; i += len)
        {
            float wr = 1.0f, wi = 0.0f;
            int   half = len >> 1;
            for(int j = 0; j < half; j++)
            {
                int   a = i + j,  b = i + j + half;
                float ur = re[a], ui = im[a];
                float vr = re[b] * wr - im[b] * wi;
                float vi = re[b] * wi + im[b] * wr;
                re[a] = ur + vr;  im[a] = ui + vi;
                re[b] = ur - vr;  im[b] = ui - vi;
                float nwr = wr * wr_step - wi * wi_step;
                wi         = wr * wi_step + wi * wr_step;
                wr         = nwr;
            }
        }
    }
}

static void fft_inverse(float *re, float *im, int n)
{
    for(int i = 0; i < n; i++) im[i] = -im[i];
    fft_forward(re, im, n);
    float inv_n = 1.0f / (float)n;
    for(int i = 0; i < n; i++) { re[i] *= inv_n; im[i] = -im[i] * inv_n; }
}

/* ==========================================================================
 * generate_bl_wavetable
 *   source       : forme d'onde source (int16_t, source_size echantillons)
 *   output       : wavetable de sortie (WAVETABLE_SIZE echantillons)
 *   max_harmonic : conserver harmoniques 1..max_harmonic
 * ========================================================================== */
static void generate_bl_wavetable(
    const int16_t *source, int source_size,
    int16_t *output, int max_harmonic)
{
    int n = WAVETABLE_SIZE;

    /* Reechantillonnage lineaire + suppression DC */
    float dc = 0.0f;
    for(int i = 0; i < n; i++)
    {
        float pos  = (float)i * (float)source_size / (float)n;
        int   idx0 = (int)pos;
        float frac = pos - (float)idx0;
        int   idx1 = (idx0 + 1 < source_size) ? idx0 + 1 : 0;
        fft_re[i] = (float)source[idx0]
                  + frac * ((float)source[idx1] - (float)source[idx0]);
        dc       += fft_re[i];
    }
    dc /= (float)n;
    for(int i = 0; i < n; i++) { fft_re[i] -= dc; fft_im[i] = 0.0f; }

    /* FFT directe */
    fft_forward(fft_re, fft_im, n);

    /* Band-limiting : zeroter DC et bins > max_harmonic (+ miroirs conjugues) */
    fft_re[0] = 0.0f; fft_im[0] = 0.0f;
    for(int k = max_harmonic + 1; k <= n / 2; k++)
    {
        fft_re[k] = 0.0f; fft_im[k] = 0.0f;
        if(k < n) { fft_re[n - k] = 0.0f; fft_im[n - k] = 0.0f; }
    }

    /* FFT inverse */
    fft_inverse(fft_re, fft_im, n);

    /* Normalisation -> int16_t */
    float max_val = 0.0f;
    for(int i = 0; i < n; i++)
    {
        float v = fabsf(fft_re[i]);
        if(v > max_val) max_val = v;
    }
    float scale = (max_val > 1e-6f) ? (30000.0f / max_val) : 0.0f;
    for(int i = 0; i < n; i++)
    {
        int32_t v = (int32_t)(fft_re[i] * scale);
        if(v >  32767) v =  32767;
        if(v < -32768) v = -32768;
        output[i] = (int16_t)v;
    }
}

/* max harmoniques par niveau BL
 * BL0 plein spectre (f<500Hz) / BL1 (f<2kHz) / BL2 (f<8kHz) / BL3 quasi-sin */
static const int bl_max_harmonic[ACCORDION_NUM_BL] = {
    WAVETABLE_SIZE / 2,   /* BL0 : toutes les harmoniques sous Nyquist */
    44,                    /* BL1 : Nyquist / 500 Hz  */
    11,                    /* BL2 : Nyquist / 2000 Hz */
    2                      /* BL3 : Nyquist / 8000 Hz */
};


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


    /* -- Sources (placeholder sinusoides, remplacer par vraies formes d'onde) */
    Accordion_Tables_Init();

    /* -- Generation des 6 x 4 = 24 wavetables band-limited ---------------- */
    int16_t * const sources[ACCORDION_NUM_WAVES] = {
        do3, sol3, do4, sol4, do5, sol5
    };

    for(int w = 0; w < ACCORDION_NUM_WAVES; w++)
    {
        for(int b = 0; b < ACCORDION_NUM_BL; b++)
        {
            generate_bl_wavetable(
                sources[w], SOURCE_SIZE,
                wavetable_accordion[w][b],
                bl_max_harmonic[b]);
        }
    }
}

