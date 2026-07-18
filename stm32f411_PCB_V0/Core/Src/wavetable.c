#include "wavetable.h"
#include <math.h>


int16_t wavetable_sine[WAVETABLE_SIZE];
int16_t wavetable_saw[WAVETABLE_SIZE];
int16_t wavetable_accordion[WAVETABLE_SIZE];


void Wavetable_Init(void)
{
    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {

        float phase =
            2.0f * (float)M_PI *
            (float)i /
            (float)WAVETABLE_SIZE;


        /*
         * Table sinus
         * amplitude proche du maximum 16 bits
         */
        wavetable_sine[i] =
            (int16_t)(32767.0f * sinf(phase));



        /*
         * Table dent de scie
         * rampe -32768 -> +32767
         */
        wavetable_saw[i] =
            (int16_t)
            (
                -32768.0f +
                (65535.0f *
                (float)i /
                (float)(WAVETABLE_SIZE - 1))
            );



        /*
         * Modèle accordéon :
         * fondamentale + harmoniques
         *
         * Les coefficients donnent
         * le timbre.
         */
        float acc =
              1.00f * sinf(phase)
            + 0.45f * sinf(2.0f * phase)
            + 0.25f * sinf(3.0f * phase)
            + 0.15f * sinf(5.0f * phase);



        /*
         * Normalisation :
         * on évite le clipping.
         */
        wavetable_accordion[i] =
            (int16_t)(acc * 10000.0f);

    }
}
