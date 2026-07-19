#include "wavetable.h"
#include "accordion_tables.h"

#include <math.h>
#include <stdint.h>


int16_t wavetable_sine[WAVETABLE_SIZE];
int16_t wavetable_saw[WAVETABLE_SIZE];

int16_t wavetable_accordion_low[WAVETABLE_SIZE];
int16_t wavetable_accordion_mid[WAVETABLE_SIZE];
int16_t wavetable_accordion_high[WAVETABLE_SIZE];



/*
===============================================================
 Charge une référence audio dans une wavetable

 - suppression offset DC
 - normalisation
 - rééchantillonnage
===============================================================
*/
static void LoadWavetable(
        const int16_t *source,
        int sourceSize,
        int16_t *destination)
{
    float offset = 0.0f;


    /*
       Calcul composante continue
    */
    for(int i = 0; i < sourceSize; i++)
    {
        offset += (float)source[i];
    }

    offset /= (float)sourceSize;



    /*
       Recherche amplitude maximale après centrage
    */
    float maxAbs = 0.0f;

    for(int i = 0; i < sourceSize; i++)
    {
        float v = (float)source[i] - offset;

        if(fabsf(v) > maxAbs)
            maxAbs = fabsf(v);
    }


    /*
       Protection division par zéro
    */
    if(maxAbs < 1.0f)
        maxAbs = 1.0f;


    /*
       Niveau final
       marge avant saturation int16
    */
    float gain = 30000.0f / maxAbs;



    /*
       Rééchantillonnage simple
    */
    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {
        int index =
            (i * sourceSize) / WAVETABLE_SIZE;


        float sample =
            ((float)source[index] - offset)
            * gain;


        if(sample > 32767.0f)
            sample = 32767.0f;

        if(sample < -32768.0f)
            sample = -32768.0f;


        destination[i] = (int16_t)sample;
    }
}




void Wavetable_Init(void)
{

    for(int i = 0; i < WAVETABLE_SIZE; i++)
    {

        float phase =
            2.0f * (float)M_PI *
            (float)i /
            (float)WAVETABLE_SIZE;



        /*
        =========================
        Sinus
        =========================
        */

        wavetable_sine[i] =
            (int16_t)
            (32767.0f * sinf(phase));



        /*
        =========================
        Saw
        =========================
        */

        wavetable_saw[i] =
            (int16_t)
            (
                -32768.0f +
                65535.0f *
                (float)i /
                (float)(WAVETABLE_SIZE-1)
            );

    }



    /*
    ==============================
    Accordéon réel
    ==============================

    Grave  : la1
    Médium : la2
    Aigu   : la3

    Chaque table est :
    - recentrée
    - normalisée
    - adaptée à WAVETABLE_SIZE
    */

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
