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
#define MSG_PIDLIST			20

#define MAX_PIDTAB_LEN			30

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

struct diseqc_master_cmd {
	u8 msg[6];
	u8 msg_len;
};

struct sat_params {
	u8	tone;
	u8	voltage;
	struct	diseqc_master_cmd diseqc_master_cmd;
	u8	burst;
};

struct fe_params {
	u32	delivery_system;
	u32	frequency;
	u8	inversion;
	union {
		struct {
			// DVB-S , DVB-S2
			u32	symbol_rate;
			u32	fec_inner;
			u32	modulation;
			u32	pilot;
			u32	rolloff;
			struct sat_params sat;
		} qpsk;
		struct {
			// DVB-C
			u32	symbol_rate;
			u32	fec_inner;
			u32	modulation;
		} qam;
		struct {
			// DVB-T
			u32	bandwidth;
			u32	code_rate_HP;
			u32	code_rate_LP;
			u32	constellation;
			u32	transmission_mode;
			u32	guard_interval;
			u32	hierarchy_information;
		} ofdm;
	} u;
};

struct vtuner_message {
	s32 type;
	union {
		struct fe_params fe_params;
		u16 pidlist[MAX_PIDTAB_LEN];
	} body;
};

struct vtuner_signal
{
	u8 status;
	u32 ber;
	u16 ss;
	u16 snr;
	u32 ucb;
};

#define VTUNER_MAJOR		226

#define VTUNER_GET_MESSAGE	_IOR(VTUNER_MAJOR, 1, struct vtuner_message *)
#define VTUNER_SET_RESPONSE 	_IOW(VTUNER_MAJOR, 2, struct vtuner_message *)
#define VTUNER_SET_SIGNAL	_IOW(VTUNER_MAJOR, 3, struct vtuner_signal *)

#endif

