/*
 * servo.c
 *
 * Clock servo to synchronize HW clocks. Inspired by servo in linuxptp implementation.
 */

#include <math.h>

#include "servo.h"
#include "clockadj.h"

struct pi_servo *servo_create(int clkid)
{
	double fadj;
	int max_adj;

	fadj = clockadj_get_freq(clkid);
	max_adj = 62499999; // phc_max_adj(clkid);

	int servo_max_frequency;

	struct pi_servo *s;
	s = calloc(1, sizeof(struct pi_servo));
	if (!s)
		return NULL;

	s->drift         = -fadj;
	s->last_freq     = -fadj;
	s->kp            = 0.0;
	s->ki            = 0.0;
	s->configured_pi_kp = 0.0;
	s->configured_pi_ki = 0.0;

	s->configured_pi_kp_scale = HWTS_KP_SCALE;
	s->configured_pi_kp_exponent = 0.0;
	s->configured_pi_kp_norm_max = MAX_KP_NORM_MAX;
	s->configured_pi_ki_scale = HWTS_KI_SCALE;
	s->configured_pi_ki_exponent = 0.0;
	s->configured_pi_ki_norm_max = MAX_KI_NORM_MAX;

	s->step_threshold = 0.0 * NSEC_PER_SEC;  // non-negative;
	s->first_step_threshold = 0.00002 * NSEC_PER_SEC;    // non-negative;
	servo_max_frequency = 900000000;
	s->max_frequency = max_adj;
	if (servo_max_frequency && s->max_frequency > servo_max_frequency) {
		s->max_frequency = servo_max_frequency;
	}

	s->first_update = 1;
	s->offset_threshold = 0;
	s->num_offset_values = 10;
	s->curr_offset_values = s->num_offset_values;

	servo_sync_interval(s, SERVO_SYNC_INTERVAL);

	return s;
}


void servo_destroy(struct pi_servo *s)
{
	free(s);
}

int check_offset_threshold(struct pi_servo *s, int64_t offset)
{
	long long int abs_offset = llabs(offset);

	if (s->offset_threshold) {
		if (abs_offset < s->offset_threshold && s->curr_offset_values)
			s->curr_offset_values--;
		return s->curr_offset_values ? 0 : 1;
	}
	return 0;
}

double servo_sample(struct pi_servo *s,
			int64_t offset,
			uint64_t local_ts,
			double weight)
{
	double ki_term, ppb = s->last_freq;
	double freq_est_interval, localdiff;

	switch (s->count) {
	case 0:
		s->offset[0] = offset;
		s->local[0] = local_ts;
		s->servo_state = SERVO_UNLOCKED;
		s->count = 1;
		break;
	case 1:
		s->offset[1] = offset;
		s->local[1] = local_ts;

		/* Make sure the first sample is older than the second. */
		if (s->local[0] >= s->local[1]) {
			s->servo_state = SERVO_UNLOCKED;
			s->count = 0;
			break;
		}

		/* Wait long enough before estimating the frequency offset. */
		localdiff = (s->local[1] - s->local[0]) / 1e9;
		localdiff += localdiff * FREQ_EST_MARGIN;
		freq_est_interval = 0.016 / s->ki;
		if (freq_est_interval > 1000.0) {
			freq_est_interval = 1000.0;
		}
		if (localdiff < freq_est_interval) {
			s->servo_state = SERVO_UNLOCKED;
			break;
		}

		/* Adjust drift by the measured frequency offset. */
		s->drift += (1e9 - s->drift) * (s->offset[1] - s->offset[0]) /
						(s->local[1] - s->local[0]);

		if (s->drift < -s->max_frequency)
			s->drift = -s->max_frequency;
		else if (s->drift > s->max_frequency)
			s->drift = s->max_frequency;

		if ((s->first_update &&
		     s->first_step_threshold &&
		     s->first_step_threshold < llabs(offset)) ||
		    (s->step_threshold &&
		     s->step_threshold < llabs(offset)))
			s->servo_state = SERVO_JUMP;
		else
			s->servo_state = SERVO_LOCKED;

		ppb = s->drift;
		s->count = 2;
		break;
	case 2:
		/*
		 * reset the clock servo when offset is greater than the max
		 * offset value. Note that the clock jump will be performed in
		 * step 1, so it is not necessary to have clock jump
		 * immediately. This allows re-calculating drift as in initial
		 * clock startup.
		 */
		if (s->step_threshold &&
		    s->step_threshold < llabs(offset)) {
			s->servo_state = SERVO_UNLOCKED;
			s->count = 0;
			break;
		}

		ki_term = s->ki * offset * weight;
		ppb = s->kp * offset * weight + s->drift + ki_term;
		if (ppb < -s->max_frequency) {
			ppb = -s->max_frequency;
		} else if (ppb > s->max_frequency) {
			ppb = s->max_frequency;
		} else {
			s->drift += ki_term;
		}
		s->servo_state = SERVO_LOCKED;
		break;
	}

	s->last_freq = ppb;

	switch (s->servo_state) {
	case SERVO_UNLOCKED:
		s->curr_offset_values = s->num_offset_values;
		break;
	case SERVO_JUMP:
		s->curr_offset_values = s->num_offset_values;
		s->first_update = 0;
		break;
	case SERVO_LOCKED:
		if (check_offset_threshold(s, offset)) {
			s->servo_state = SERVO_LOCKED_STABLE;
		}
		s->first_update = 0;
		break;
	case SERVO_LOCKED_STABLE:
		/*
		 * This case will never occur since the only place
		 * SERVO_LOCKED_STABLE is set is in this switch/case block
		 * (case SERVO_LOCKED).
		 */
		break;
	}

    return ppb;
}

void servo_sync_interval(struct pi_servo *s, double interval)
{
	s->kp = s->configured_pi_kp_scale * pow(interval, s->configured_pi_kp_exponent);
	if (s->kp > s->configured_pi_kp_norm_max / interval)
		s->kp = s->configured_pi_kp_norm_max / interval;

	s->ki = s->configured_pi_ki_scale * pow(interval, s->configured_pi_ki_exponent);
	if (s->ki > s->configured_pi_ki_norm_max / interval)
		s->ki = s->configured_pi_ki_norm_max / interval;

	printf("PI servo: sync interval %.3f kp %.3f ki %.6f\n",
		 interval, s->kp, s->ki);
}

void servo_reset(struct pi_servo *s)
{
	s->count = 0;
}