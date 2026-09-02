
#include "wavetable.h"
#include "accordion_tables.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* Taille utilisee UNIQUEMENT pour l'analyse spectrale (FFT), independante de
   WAVETABLE_SIZE : le spectre ne change pas si WAVETABLE_SIZE change. */
#define ANALYSIS_SIZE 2048


/* ==========================================================================
 * Wavetables
 * ========================================================================== */

int16_t wavetable_sine[WAVETABLE_SIZE];

/* [position_musicale : 0=do3..5=sol5][niveau_BL : 0=plein..3=sinus][echantillon] */
int16_t wavetable_accordion[ACCORDION_NUM_WAVES][ACCORDION_NUM_BL][WAVETABLE_SIZE];

/* ==========================================================================
 * FFT radix-2 Cooley-Tukey (float, sur place)
 * Buffers statiques : 2 x 2048 x 4 = 16 Ko — utilises uniquement a Wavetable_Init.
 * ========================================================================== */

static float fft_re[ANALYSIS_SIZE];
static float fft_im[ANALYSIS_SIZE];

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
 * resample_periodic
 *   Interpolation lineaire d'une forme d'onde periodique.
 *   source_size -> destination_size
 * ========================================================================== */
static float resample_periodic(
    const float *source, int source_size,
    int index, int destination_size)
{
    float pos  = (float)index * (float)source_size / (float)destination_size;
    int   idx0 = (int)pos;
    float frac = pos - (float)idx0;
    if(idx0 >= source_size) idx0 = 0;
    int   idx1 = (idx0 + 1 < source_size) ? idx0 + 1 : 0;
    return source[idx0] + frac * (source[idx1] - source[idx0]);
}

/* ==========================================================================
 * generate_bl_wavetable
 *   source       : forme d'onde source (int16_t, source_size echantillons)
 *   output       : wavetable de sortie (WAVETABLE_SIZE echantillons)
 *   max_harmonic : conserver harmoniques 1..max_harmonic
 *
 * Le spectre est calcule dans ANALYSIS_SIZE, independant de WAVETABLE_SIZE ;
 * cette derniere sert uniquement a produire la table finale (meme contenu
 * spectral quelle que soit sa taille : 256 / 512 / 1024...).
 * ========================================================================== */
static void generate_bl_wavetable(
    const int16_t * const source, int source_size,
    int16_t *output, int max_harmonic)
{
    int n = ANALYSIS_SIZE;

    /* Reechantillonnage lineaire vers la taille d'analyse + suppression DC */
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
    int max_allowed = (max_harmonic > n / 2) ? n / 2 : max_harmonic;
    for(int k = max_allowed + 1; k <= n / 2; k++)
    {
        fft_re[k] = 0.0f; fft_im[k] = 0.0f;
        if(k < n) { fft_re[n - k] = 0.0f; fft_im[n - k] = 0.0f; }
    }

    /* FFT inverse */
    fft_inverse(fft_re, fft_im, n);

    /* Normalisation (avant le changement de taille) */
    float max_val = 0.0f;
    for(int i = 0; i < n; i++)
    {
        float v = fabsf(fft_re[i]);
        if(v > max_val) max_val = v;
    }

    if(max_val < 1e-6f)
    {
        memset(output, 0, sizeof(int16_t) * WAVETABLE_SIZE);
        return;
    }

    float scale = 30000.0f / max_val;

    /* Reechantillonnage final vers WAVETABLE_SIZE -> int16_t */
    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {
        float value = resample_periodic(fft_re, n, i, WAVETABLE_SIZE);
        int32_t v = (int32_t)(value * scale);
        if(v >  32767) v =  32767;
        if(v < -32768) v = -32768;
        output[i] = (int16_t)v;
    }
}

/* max harmoniques par niveau BL (numeros d'harmoniques, independants de
 * WAVETABLE_SIZE) : BL0 plein spectre / BL1 (f<2kHz) / BL2 (f<8kHz) / BL3 quasi-sin */
static const int bl_max_harmonic[ACCORDION_NUM_BL] = {
    ANALYSIS_SIZE / 2,   /* BL0 : plein spectre */
    44,                    /* BL1 */
    11,                    /* BL2 */
    2                      /* BL3 */
};


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


    /* -- Generation des 6 x 4 = 24 wavetables band-limited ---------------- */
    const int16_t * const sources[ACCORDION_NUM_WAVES] = {
        do3, sol3, do4, sol4, do5, sol5
    };

    static const int source_sizes[ACCORDION_NUM_WAVES] = {
        DO3_SIZE, SOL3_SIZE, DO4_SIZE, SOL4_SIZE, DO5_SIZE, SOL5_SIZE
    };

    for(int w = 0; w < ACCORDION_NUM_WAVES; w++)
    {
        for(int b = 0; b < ACCORDION_NUM_BL; b++)
        {
            generate_bl_wavetable(
                sources[w], source_sizes[w],
                wavetable_accordion[w][b],
                bl_max_harmonic[b]);
        }
    }
}

