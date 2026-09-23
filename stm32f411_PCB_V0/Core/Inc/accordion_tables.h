#ifndef ACCORDION_TABLES_H
#define ACCORDION_TABLES_H

#include <stdint.h>

/* Taille de chaque periode source (echantillons mesures) */
#define DO3_SIZE   335
#define SOL3_SIZE   224
#define DO4_SIZE   168
#define SOL4_SIZE   112
#define DO5_SIZE   84
#define SOL5_SIZE   56

extern const int16_t do3 [DO3_SIZE];
extern const int16_t sol3 [SOL3_SIZE];
extern const int16_t do4 [DO4_SIZE];
extern const int16_t sol4 [SOL4_SIZE];
extern const int16_t do5 [DO5_SIZE];
extern const int16_t sol5 [SOL5_SIZE];

#endif /* ACCORDION_TABLES_H */
