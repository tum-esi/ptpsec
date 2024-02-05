/**
 * @file servo.h
 *
 * Clock servo to synchronize HW clocks. Inspired by servo in linuxptp implementation.
 */

#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>


#define HWTS_KP_SCALE 0.7
#define HWTS_KI_SCALE 0.3
#define SWTS_KP_SCALE 0.1
#define SWTS_KI_SCALE 0.001

#define MAX_KP_NORM_MAX 1.0
#define MAX_KI_NORM_MAX 2.0

#define FREQ_EST_MARGIN 0.001

#define SERVO_SYNC_INTERVAL 1.0

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC		1000000000L
#endif
#define SAMPLE_WEIGHT		1.0

/**
 * Defines the caller visible states of a clock servo.
 */
enum servo_state {

	/**
	 * The servo is not yet ready to track the master clock.
	 */
	SERVO_UNLOCKED,

	/**
	 * The is ready to track and requests a clock jump to
	 * immediately correct the estimated offset.
	 */
	SERVO_JUMP,

	/**
	 * The servo is tracking the master clock.
	 */
	SERVO_LOCKED,

	/**
	 * The Servo has stabilized. The last 'servo_num_offset_values' values
	 * of the estimated threshold are less than servo_offset_threshold.
	 */
	SERVO_LOCKED_STABLE,
};

struct pi_servo {
    /* general servo settings */
    double max_frequency;
	double step_threshold;
	double first_step_threshold;
	int first_update;
	int64_t offset_threshold;
	int num_offset_values;
	int curr_offset_values;
    /* pi servo */
	int64_t offset[2];
	uint64_t local[2];
	double drift;
	double kp;
	double ki;
	double last_freq;
	int count;
    /* current state */
    enum servo_state servo_state;
	/* configuration: */
	double configured_pi_kp;
	double configured_pi_ki;
	double configured_pi_kp_scale;
	double configured_pi_kp_exponent;
	double configured_pi_kp_norm_max;
	double configured_pi_ki_scale;
	double configured_pi_ki_exponent;
	double configured_pi_ki_norm_max;
};


struct pi_servo *servo_create(int clkid);
void servo_destroy(struct pi_servo *servo);
double servo_sample(struct pi_servo *servo,
		    int64_t offset,
		    uint64_t local_ts,
		    double weight);
void servo_sync_interval(struct pi_servo *servo, double interval);
void servo_reset(struct pi_servo *servo);
int servo_offset_threshold(struct pi_servo *servo);

#endif
