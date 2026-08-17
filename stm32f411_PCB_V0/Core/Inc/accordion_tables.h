#ifndef ACCORDION_TABLES_H
#define ACCORDION_TABLES_H

#include <stdint.h>

/* Taille de chaque periode source (echantillons mesures) */
#define DO3_SIZE   336
#define SOL3_SIZE  225
#define DO4_SIZE   144
#define SOL4_SIZE  111
#define DO5_SIZE   72
#define SOL5_SIZE  51

/* Frequences de reference des 6 positions musicales :
   do3  = 130.81 Hz   sol3 = 196.00 Hz
   do4  = 261.63 Hz   sol4 = 392.00 Hz
   do5  = 523.25 Hz   sol5 = 783.99 Hz  */
extern const int16_t do3 [DO3_SIZE];
extern const int16_t sol3[SOL3_SIZE];
extern const int16_t do4 [DO4_SIZE];
extern const int16_t sol4[SOL4_SIZE];
extern const int16_t do5 [DO5_SIZE];
extern const int16_t sol5[SOL5_SIZE];

#endif /* ACCORDION_TABLES_H */
