#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stdint.h>

#define WAVETABLE_SIZE 256


extern int16_t wavetable_sine[WAVETABLE_SIZE];
extern int16_t wavetable_saw[WAVETABLE_SIZE];
extern int16_t wavetable_accordion_low[WAVETABLE_SIZE];
extern int16_t wavetable_accordion_mid[WAVETABLE_SIZE];
extern int16_t wavetable_accordion_high[WAVETABLE_SIZE];


void Wavetable_Init(void);


#endif
