/*
 * vtunerc: /dev/vtunerc API
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [based on dreamtuner userland code by Roland Mieslinger]
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _VTUNER_H_
#define _VTUNER_H_

#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#define MSG_SET_FRONTEND		10
#define MSG_CLOSE_FRONTEND		11
#define MSG_PIDLIST			20

#define MAX_PIDTAB_LEN			128

#define PID_UNKNOWN 0xffff

#ifndef __KERNEL__
#ifndef u64
typedef unsigned long long u64;
#endif

#ifndef s64
typedef long long s64;
#endif

#ifndef u32
typedef unsigned int u32;
#endif

#ifndef s32
typedef signed int s32;
#endif

#ifndef u16
typedef unsigned short u16;
#endif

#ifndef u8
typedef unsigned char u8;
#endif
#endif

struct diseqc_master_cmd {
	u8 msg[6];
	u8 msg_len;
} __attribute__ ((packed));

struct burst_cmd {
	u8 value;
	u8 valid;
} __attribute__ ((packed));

struct sat_params {
	u8	tone;
	u8	voltage;
	struct	diseqc_master_cmd diseqc_master_cmd;
	struct  burst_cmd burst_cmd;
} __attribute__ ((packed));

struct fe_params {
	u32	delivery_system;
	u32	frequency;
	u32	modulation;
	union {
		struct {
			// DVB-S , DVB-S2
			u32	symbol_rate;
			u32	fec_inner;
			u32	pilot;
			u32	rolloff;
			struct sat_params sat;
		} qpsk;
		struct {
			// DVB-C
			u32	symbol_rate;
			u32	inversion;
		} qam;
		struct {
			// DVB-T
			u32	bandwidth;
			u32	code_rate_HP;
			u32	code_rate_LP;
			u32	transmission_mode;
			u32	guard_interval;
			u32	hierarchy_information;
		} ofdm;
	} u;
} __attribute__ ((packed));

struct fe_tune {
	u8    tune_id;
	struct fe_params fe_params;
} __attribute__ ((packed));

struct vtuner_message {
	s32 type;
	union {
		struct fe_tune fe_tune;
		u16 pidlist[MAX_PIDTAB_LEN];
	} body;
} __attribute__ ((packed));

#define VTUNER_MAX_DTV_STATS 4

enum vtuner_scale_params {
	VT_SCALE_NOT_AVAILABLE = 0,
	VT_SCALE_DECIBEL,
	VT_SCALE_RELATIVE,
	VT_SCALE_COUNTER
};

struct vtuner_dtv_stats {
	u8 scale;
	union {
		u64 uvalue;
		s64 svalue;
	} u;
} __attribute__ ((packed));

struct vtuner_dtv_fe_stats {
	u8 len;
	struct vtuner_dtv_stats stat[MAX_DTV_STATS];
} __attribute__ ((packed));

struct vtuner_signal {
	struct vtuner_dtv_fe_stats strength;
	struct vtuner_dtv_fe_stats cnr;
	struct vtuner_dtv_fe_stats pre_bit_error;
	struct vtuner_dtv_fe_stats pre_bit_count;
	struct vtuner_dtv_fe_stats post_bit_error;
	struct vtuner_dtv_fe_stats post_bit_count;
	struct vtuner_dtv_fe_stats block_error;
	struct vtuner_dtv_fe_stats block_count;
} __attribute__ ((packed));

#define VTUNER_MAX_DELSYS 8

struct vtuner_delsys {
	u8 value[VTUNER_MAX_DELSYS];
} __attribute__ ((packed));

#define VTUNER_MAJOR		226

#define VTUNER_GET_MESSAGE	_IOR(VTUNER_MAJOR, 1, struct vtuner_message)
#define VTUNER_SET_RESPONSE 	_IOW(VTUNER_MAJOR, 2, struct vtuner_message)
#define VTUNER_SET_SIGNAL	_IOW(VTUNER_MAJOR, 3, struct vtuner_signal)
#define VTUNER_SET_DELSYS	_IOW(VTUNER_MAJOR, 4, struct vtuner_delsys)

#endif

