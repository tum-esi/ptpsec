#ifndef MITM_H
#define MITM_H


#include "servo.h"


/**
 * The structure of a PTP V2 packet.
 *
 * Only the minimum fields used by the ieee1588 test are represented.
 */
struct __attribute__((__packed__)) ptpv21_msg
{
	uint8_t msg_type;			/* Message Type/Id (1 B)*/
	uint8_t version;			/* PTP Version (1 B)*/
	uint16_t msg_len;			/* Message Length (2 B)*/
	uint8_t domain_number;		/* Domain Number (1 B)*/
	uint8_t minor_sdo_id;		/* Minor Sdo Id (1 B)*/
	uint16_t flags;				/* Flags (2 B)*/
	uint64_t correction_field;	/* Correction Field (8 B)*/
	uint32_t msg_type_specific; /* Message Type Specific (4 B)*/
	uint8_t src_port_id[10];	/* Source Port Identity (10 B)*/
	uint16_t seq_id;			/* Sequence Id (2 B)*/
	uint8_t control_field;		/* Control Field (1 B)*/
	uint8_t log_msg_int;		/* logMessageInterval (1 B)*/
};

struct __attribute__((__packed__)) meas_hdr
{
	uint32_t type;			   /* Measurement Msg Type (4 B) */
	struct timespec timestamp; /* Some timespec value (10 B)*/
};

/*
 * Data structure to save packet residence times to put
 * into the correction field of a subsequent message
 */
struct residence_time_entry
{
	LIST_ENTRY(residence_time_entry)
	list;					 // First element in linked list
	uint16_t seq_id;		 // Packet sequence number
	uint64_t residence_time; // Packet residence time in ns
};

/*
 * Data structure for saving residence times for selected messages
 */
LIST_HEAD(res_time_head, residence_time_entry);
struct res_time_head res_time_sync;
struct res_time_head res_time_dreq;
struct res_time_head res_time_meas;

/*
 * Servo for internal clock synchronization
 */
struct pi_servo *servo;


/*
 * PTP message types (event messages)
 */
#define PTP_SYNC_MESSAGE 0x0
#define PTP_DELAY_REQ_MESSAGE 0x1
#define PTP_PATH_DELAY_REQ_MESSAGE 0x2
#define PTP_PATH_DELAY_RESP_MESSAGE 0x3
#define PTP_MEAS 0x4

/*
 * PTP message types (general messages)
 */
#define PTP_FOLLOWUP_MESSAGE 0x8
#define PTP_DELAY_RESP_MESSAGE 0x9
#define PTP_PATH_DELAY_FOLLOWUP_MESSAGE 0xA
#define PTP_ANNOUNCE_MESSAGE 0xB
#define PTP_SIGNALLING_MESSAGE 0xC
#define PTP_MANAGEMENT_MESSAGE 0xD

/*
 * PTP MEAS message types
 */
#define PTP_MEAS_MEASUREMENT 0x0
#define PTP_MEAS_FOLLOW_UP 0x1
#define PTP_MEAS_TRANSPORT 0x2

/*
 * Time conversion constants
 */
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif
#define US_TO_NS 1000

/*
 * Function declarations
 */
void synchronize_clocks(void);
// static int port_ieee1588_read_rx_timestamp(uint16_t port, uint32_t index, struct timespec *timestamp);


static inline uint64_t timespec64_to_ns(const struct timespec *ts)
{
	return ((uint64_t)ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

#endif