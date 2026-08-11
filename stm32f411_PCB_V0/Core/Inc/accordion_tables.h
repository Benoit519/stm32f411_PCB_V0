#ifndef ACCORDION_TABLES_H
#define ACCORDION_TABLES_H

#include <stdint.h>

/* Taille de chaque source : une periode complete (puissance de 2 = WAVETABLE_SIZE) */
#define SOURCE_SIZE  512

/* Frequences de reference des 6 positions musicales :
   do3  = 130.81 Hz   sol3 = 196.00 Hz
   do4  = 261.63 Hz   sol4 = 392.00 Hz
   do5  = 523.25 Hz   sol5 = 783.99 Hz  */
extern int16_t do3 [SOURCE_SIZE];
extern int16_t sol3[SOURCE_SIZE];
extern int16_t do4 [SOURCE_SIZE];
extern int16_t sol4[SOURCE_SIZE];
extern int16_t do5 [SOURCE_SIZE];
extern int16_t sol5[SOURCE_SIZE];

/* Remplit les sources (placeholder sinusoides ; remplacer par vraies formes d'onde) */
void Accordion_Tables_Init(void);

#endif /* ACCORDION_TABLES_H */
