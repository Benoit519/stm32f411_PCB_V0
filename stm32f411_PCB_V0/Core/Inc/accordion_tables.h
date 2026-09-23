#ifndef ACCORDION_TABLES_H
#define ACCORDION_TABLES_H

#include <stdint.h>

/* Taille de chaque periode source (echantillons mesures) */
#define DO3_ATTACK_SIZE   346
#define DO3_SIZE   334
#define SOL3_ATTACK_SIZE   231
#define SOL3_SIZE   223
#define DO4_ATTACK_SIZE   169
#define DO4_SIZE   113
#define SOL4_ATTACK_SIZE   113
#define SOL4_SIZE   112
#define DO5_ATTACK_SIZE   94
#define DO5_SIZE   84
#define SOL5_ATTACK_SIZE   56
#define SOL5_SIZE   56

extern const int16_t do3_attack [DO3_ATTACK_SIZE];
extern const int16_t do3 [DO3_SIZE];
extern const int16_t sol3_attack [SOL3_ATTACK_SIZE];
extern const int16_t sol3 [SOL3_SIZE];
extern const int16_t do4_attack [DO4_ATTACK_SIZE];
extern const int16_t do4 [DO4_SIZE];
extern const int16_t sol4_attack [SOL4_ATTACK_SIZE];
extern const int16_t sol4 [SOL4_SIZE];
extern const int16_t do5_attack [DO5_ATTACK_SIZE];
extern const int16_t do5 [DO5_SIZE];
extern const int16_t sol5_attack [SOL5_ATTACK_SIZE];
extern const int16_t sol5 [SOL5_SIZE];

#endif /* ACCORDION_TABLES_H */
