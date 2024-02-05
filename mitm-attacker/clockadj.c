#include "clockadj.h"

/*
 * Mode codes (timex.mode)
 */
// #define ADJ_OFFSET 0x0001    /* time offset */
// #define ADJ_FREQUENCY 0x0002 /* frequency offset */
// #define ADJ_MAXERROR 0x0004  /* maximum time error */
// #define ADJ_ESTERROR 0x0008  /* estimated time error */
// #define ADJ_STATUS 0x0010    /* clock status */
// #define ADJ_TIMECONST 0x0020 /* pll time constant */
// #define ADJ_TAI 0x0080       /* set TAI offset */
// #define ADJ_SETOFFSET 0x0100 /* add 'time' to current time */
// #define ADJ_MICRO 0x1000     /* select microsecond resolution */
// #define ADJ_NANO 0x2000      /* select nanosecond resolution */
// #define ADJ_TICK 0x4000      /* tick value */

int clock_adjtime(int clkid, struct timex *tx)
{
    int err = 0;
    int64_t step;
    uint32_t inc_val;
    uint16_t sign = 0;  // positive
    int64_t delta;  // [ns]

    // Select mode   
    switch (tx->modes & 0xFFF)
    {
    case ADJ_SETOFFSET:
        // Adjust clock
        step = (tx->time.tv_sec * NSEC_PER_SEC) + (tx->time.tv_usec * 1);
        printf("Step: %ld\n", step);
        // err = rte_eth_timesync_adjust_time(clkid, step);
        err = rte_eth_timesync_adj_val(clkid, step);
        break;
    case ADJ_FREQUENCY:
        // Calculate freq inc value (refer to i210 data sheet)
        // inc_val: sign (1bit) + value (31bit) * 2^(-32)ns [-0.5 ... +0.5]
        // inc_time = 8ns +/- inc_val * 2^(-32)ns (each 8ns)
        // --> 1ppm <=> inc_val = 8*10^(-6) = 2^(-17)

        // printf("TX->FREQ: %ld\n", tx->freq);

        if (tx->freq < 0)
            sign = 1;    // negative
        inc_val = tx->freq >> (7);  // convert 2^(-16)ppm to 2^(-32)ns per 8ns
        inc_val &= 0x7FFFFFFF;      // Clear sign bit

        // printf("INC_VAL: %ld\n", inc_val);

        inc_val |= (sign << 31);    // Set sign bit
        // printf("Inc Val: %08x\n", inc_val);

       
        // From timex spec: tx->freq [2^-16 ppm]
        // 2^16=65536 is 1 ppm
        // Freq = 125MHz (each 8ns, there is an update)
        delta = (int64_t)(tx->freq * 1e3) >> 16;
        // printf("PPM (2^-16): %ld\n", tx->freq);
        // printf("Delta [ns]:  %ld\n", delta);

        // Adjust clock
        err = rte_eth_timesync_adj_val(clkid, inc_val);
        // err = rte_eth_timesync_adj_val(clkid, delta);
        // err = rte_eth_timesync_adjust_time(clkid, adj_val);
        break;
    default:
        printf("Unknown Adjtime Mode: %d\n", tx->modes);
    }

    return err;
}

void clockadj_step(int clkid, int64_t step)
{
    struct timex tx;
    int sign = 1;
    if (step < 0)
    {
        sign = -1;
        step *= -1;
    }
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    tx.time.tv_sec = sign * (step / NSEC_PER_SEC);
    tx.time.tv_usec = sign * (step % NSEC_PER_SEC);
    /*
     * The value of a timeval is the sum of its fields, but the
     * field tv_usec must always be non-negative.
     */
    if (tx.time.tv_usec < 0)
    {
        tx.time.tv_sec -= 1;
        tx.time.tv_usec += 1000000000;
    }
    if (clock_adjtime(clkid, &tx) < 0)
        printf("failed to step clock: %m\n");
}

void clockadj_set_freq(int clkid, double freq)
{

    struct timex tx;
    memset(&tx, 0, sizeof(tx));

    // /* With system clock set also the tick length. */
    // if (clkid == CLOCK_REALTIME && realtime_nominal_tick) {
    // 	tx.modes |= ADJ_TICK;
    // 	tx.tick = round(freq / 1e3 / realtime_hz) + realtime_nominal_tick;
    // 	freq -= 1e3 * realtime_hz * (tx.tick - realtime_nominal_tick);
    // }

    tx.modes |= ADJ_FREQUENCY;
    tx.freq = (long)(freq * 65.536);

    if (clock_adjtime(clkid, &tx) < 0)
        printf("failed to adjust the clock: %m\n");
}

double clockadj_get_freq(int clkid)
{
    double f = 0.0;
    struct timex tx;
    memset(&tx, 0, sizeof(tx));
    if (clock_adjtime(clkid, &tx) < 0)
    {
        printf("failed to read out the clock frequency adjustment: %m\n");
        exit(1);
    }
    else
    {
        f = tx.freq / 65.536;
        // if (clkid == CLOCK_REALTIME && realtime_nominal_tick && tx.tick)
        // 	f += 1e3 * realtime_hz * (tx.tick - realtime_nominal_tick);
    }
    return f;
}
