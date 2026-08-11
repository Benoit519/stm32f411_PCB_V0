#include "accordion_tables.h"
#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

int16_t do3 [SOURCE_SIZE];
int16_t sol3[SOURCE_SIZE];
int16_t do4 [SOURCE_SIZE];
int16_t sol4[SOURCE_SIZE];
int16_t do5 [SOURCE_SIZE];
int16_t sol5[SOURCE_SIZE];

/* ==========================================================================
 * Accordion_Tables_Init
 *
 * Placeholder : sinusoide pure sur une periode complete.
 * Remplacer chaque tableau par la vraie forme d'onde d'accordeon enregistree.
 * ========================================================================== */
void Accordion_Tables_Init(void)
{
    for(int i = 0; i < SOURCE_SIZE; i++)
    {
        int16_t s = (int16_t)(30000.0f * sinf(2.0f * (float)M_PI * i / (float)SOURCE_SIZE));
        do3[i]  = s;
        sol3[i] = s;
        do4[i]  = s;
        sol4[i] = s;
        do5[i]  = s;
        sol5[i] = s;
    }
}
