#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stdint.h>

/* Taille de chaque wavetable (puissance de 2 obligatoire pour le DDS) */
#define WAVETABLE_SIZE       512

/* Dimensions du banc de wavetables accordeon band-limited */
#define ACCORDION_NUM_WAVES  6   /* do3, sol3, do4, sol4, do5, sol5          */
#define ACCORDION_NUM_BL     4   /* BL0 = plein spectre -> BL3 = quasi-sinus */

/* wavetable_accordion[position_musicale][niveau_BL][echantillon]
   Exemple : wavetable_accordion[2][1] = do4 avec niveau BL1             */
extern int16_t wavetable_accordion[ACCORDION_NUM_WAVES][ACCORDION_NUM_BL][WAVETABLE_SIZE];

/* Sinus pur utilise par le LFO global */
extern int16_t wavetable_sine[WAVETABLE_SIZE];

/* Genere toutes les wavetables a partir des sources accordion_tables */
void Wavetable_Init(void);

#endif /* WAVETABLE_H */
