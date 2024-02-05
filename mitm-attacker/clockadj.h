#ifndef CLOCKADJ_H
#define CLOCKADJ_H

#include <stdint.h>
#include <inttypes.h>
#include <time.h>

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

int clock_adjtime(int clkid, struct timex *tx);

void clockadj_step(int clkid, int64_t step);
void clockadj_set_freq(int clkid, double freq);
double clockadj_get_freq(int clkid);

#endif