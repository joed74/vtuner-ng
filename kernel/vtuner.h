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

#define VT_NULL	0
#define VT_S	5
#define VT_S2	6
#define VT_C	19
#define VT_T	16

#define MSG_SET_FRONTEND		10
#define MSG_SET_TONE			12
#define MSG_SET_VOLTAGE			13
#define MSG_SEND_DISEQC_MSG		14
#define MSG_SEND_DISEQC_BURST		15
#define MSG_PIDLIST			16

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

struct vtuner_message {
	s32 type;
	union {
		struct {
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
					u32	delivery_system;
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
		} fe_params;
		u8 tone;
		u8 voltage;
		struct diseqc_master_cmd diseqc_master_cmd;
		u8 burst;
		u16 pidlist[MAX_PIDTAB_LEN];
	} body;
};

struct vtuner_signal
{
	u32 ber;
	u16 ss;
	u16 snr;
	u32 ucb;
};

#define VTUNER_MAJOR		226

#define VTUNER_GET_MESSAGE	_IOR(VTUNER_MAJOR, 1, struct vtuner_message *)
#define VTUNER_SET_RESPONSE 	_IOW(VTUNER_MAJOR, 2, struct vtuner_message *)
#define VTUNER_SET_SIGNAL	_IOW(VTUNER_MAJOR, 3, struct vtuner_signal *)
#define VTUNER_SET_TYPE		_IOW(VTUNER_MAJOR, 4, char *)

#endif

