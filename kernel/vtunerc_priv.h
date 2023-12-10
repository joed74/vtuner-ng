/*
 * vtunerc: Internal defines
 *
 * Copyright (C) 2010-11 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
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

#ifndef _VTUNERC_PRIV_H
#define _VTUNERC_PRIV_H

#include <linux/module.h>	/* Specifically, a module */
#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/time.h>
#include <linux/cdev.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,16,0)
 #error linux kernel version 4.16 or newer required
#endif

#define HZ_FREQUENCIES

#include <media/demux.h>
#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>
#include <media/dvbdev.h>

#include "vtuner.h"

#define PID_UNKNOWN 0x0FFFF

struct vtunerc_config {

	int debug;
	int tscheck;
	int devices;
};

struct vtunerc_ctx {

	/* DVB api */
	struct dmx_frontend hw_frontend;
	struct dmx_frontend mem_frontend;
	struct dmxdev dmxdev;
	struct dvb_adapter dvb_adapter;
	struct dvb_demux demux;
	struct dvb_frontend *fe;

	/* internals */
	int idx;
	struct vtunerc_config *config;
	struct vtuner_signal signal;
	struct fe_params fe_params;

	unsigned short pidtab[MAX_PIDTAB_LEN];
	unsigned char pusitab[MAX_PIDTAB_LEN];
	struct dvb_demux_feed *feedtab[MAX_PIDTAB_LEN];

	struct semaphore xchange_sem;
	struct semaphore ioctl_sem;
	struct semaphore tswrite_sem;
	int fd_opened;

	char *procname;

	char *kernel_buf;
	ssize_t kernel_buf_size;

	/* ctrldev */
	int noresponse;
	struct vtuner_message ctrldev_request;
	struct vtuner_message ctrldev_response;
	wait_queue_head_t ctrldev_wait_request_wq;
	wait_queue_head_t ctrldev_wait_response_wq;
	int nextpacket;
	wait_queue_head_t ctrldev_wait_packet_wq;

	/* proc statistics */
	unsigned int stat_wr_data;
	time64_t stat_time;
};

int vtunerc_register_ctrldev(struct vtunerc_ctx *ctx);
void vtunerc_unregister_ctrldev(struct vtunerc_config *config);
struct vtunerc_ctx *vtunerc_get_ctx(int minor);
int /*__devinit*/ vtunerc_frontend_init(struct vtunerc_ctx *ctx);
int /*__devinit*/ vtunerc_frontend_clear(struct vtunerc_ctx *ctx);
int vtunerc_ctrldev_xchange_message(struct vtunerc_ctx *ctx, struct vtuner_message *msg, int wait4response);
int pidtab_find_index(unsigned short *pidtab, int pid);
#define dprintk(ctx, fmt, arg...) do {				\
if (ctx->config && (ctx->config->debug))			\
	printk(KERN_DEBUG "vtunerc%d: " fmt, ctx->idx, ##arg);	\
} while (0)

#endif
