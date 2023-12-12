/*
 * vtunerc: Virtual adapter driver
 *
 * Copyright (C) 2010-12 Honza Petrous <jpetrous@smartimp.cz>
 * [Created 2010-03-23]
 * Sponsored by Smartimp s.r.o. for its NessieDVB.com box
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

#include <linux/module.h>	/* Specifically, a module */
#include <linux/kernel.h>	/* We're doing kernel work */
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include "vtunerc_priv.h"

#include <media/demux.h>
#include <media/dmxdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>
#include <media/dvbdev.h>

#define VTUNERC_MODULE_VERSION "2.0"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define DRIVER_NAME	"vTuner proxy"

#define VTUNERC_PROC_FILENAME	"vtunerc%i"

#ifndef VTUNERC_MAX_ADAPTERS
#define VTUNERC_MAX_ADAPTERS	8
#endif

static struct vtunerc_ctx *vtunerc_tbl[VTUNERC_MAX_ADAPTERS] = { NULL };

/* module params */
static struct vtunerc_config config = {
	.devices = 1,
	.debug = 0
};

int pidtab_find_index(unsigned short *pidtab, int pid)
{
	int i = 0;

	while (i < MAX_PIDTAB_LEN) {
		if (pidtab[i] == pid)
			return i;
		i++;
	}

	return -1;
}

static int pidtab_add_pid(struct vtunerc_ctx *ctx, struct dvb_demux_feed *feed)
{
	int i;
	for (i = 0; i < MAX_PIDTAB_LEN; i++)
		if (ctx->pidtab[i] == PID_UNKNOWN) {
			ctx->pidtab[i] = feed->pid;
			ctx->pusitab[i] = 0;
			ctx->feedtab[i] = feed;
			return 0;
		}
	return -1;
}

static int pidtab_del_pid(struct vtunerc_ctx *ctx, struct dvb_demux_feed *feed)
{
	int i;
	for (i = 0; i < MAX_PIDTAB_LEN; i++)
		if (ctx->pidtab[i] == feed->pid) {
			ctx->pidtab[i] = PID_UNKNOWN;
			ctx->pusitab[i] = 0;
			ctx->feedtab[i] = NULL;
			return 0;
		}

	return -1;
}

static void pidtab_copy_to_msg(struct vtunerc_ctx *ctx, struct vtuner_message *msg)
{
	int i;

	for (i = 0; i < MAX_PIDTAB_LEN ; i++)
		msg->body.pidlist[i] = ctx->pidtab[i];
}

static int vtunerc_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vtunerc_ctx *ctx = demux->priv;
	struct vtuner_message msg;

	switch (feed->type) {
	case DMX_TYPE_TS:
		break;
	case DMX_TYPE_SEC:
		break;
	default:
		printk(KERN_ERR "vtunerc%d: feed type %d is not supported\n", ctx->idx, feed->type);
		return -EINVAL;
	}

	if (feed->pid >= 0x2000 ) {
	  printk(KERN_ERR "vtunerc%d: full mux not supported\n",ctx->idx);
	  return -EINVAL;
	}

	dprintk(ctx, "add pid %i\n", feed->pid);

	/* organize PID list table */
	if (pidtab_find_index(ctx->pidtab, feed->pid) < 0) {
		pidtab_add_pid(ctx, feed);

		pidtab_copy_to_msg(ctx, &msg);

		msg.type = MSG_PIDLIST;
		vtunerc_ctrldev_xchange_message(ctx, &msg, 0);
	}

	return 0;
}

static int vtunerc_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct vtunerc_ctx *ctx = demux->priv;
	struct vtuner_message msg;

	dprintk(ctx, "del pid %i\n", feed->pid);

	/* organize PID list table */
	if (pidtab_find_index(ctx->pidtab, feed->pid) > -1) {
		pidtab_del_pid(ctx, feed);

		pidtab_copy_to_msg(ctx, &msg);

		msg.type = MSG_PIDLIST;
		vtunerc_ctrldev_xchange_message(ctx, &msg, 0);
	}

	return 0;
}

/* ----------------------------------------------------------- */


#ifdef CONFIG_PROC_FS

static void status2str(struct seq_file *seq, u8 status)
{
	seq_puts(seq, "  Status      : ");
	switch (status) {
		case FE_HAS_SIGNAL:
		  seq_puts(seq, "FE_HAS_SIGNAL");
		  break;
		case FE_HAS_CARRIER:
		  seq_puts(seq, "FE_HAS_CARRIER");
		  break;
		case FE_HAS_VITERBI:
		  seq_puts(seq, "FE_HAS_VITERBI");
		  break;
		case FE_HAS_SYNC:
		  seq_puts(seq, "FE_HAS_SYNC");
		  break;
		case FE_HAS_LOCK:
		  seq_puts(seq, "FE_HAS_LOCK");
		  break;
		case FE_TIMEDOUT:
		  seq_puts(seq, "FE_TIMEDOUT");
		  break;
		case FE_REINIT:
		  seq_puts(seq, "FE_REINIT");
		  break;
		default:
		  seq_puts(seq, "FE_NONE");
	}
	seq_puts(seq, "\n");
}

static void delsys2str(struct seq_file *seq, enum fe_delivery_system delsys)
{
	seq_puts(seq, "  System      : ");
	switch (delsys) {
		case SYS_DVBC_ANNEX_A:
		case SYS_DVBC_ANNEX_B:
		case SYS_DVBC_ANNEX_C:
		  seq_puts(seq, "DVB-C");
		  break;
		case SYS_DVBT:
		  seq_puts(seq, "DVB-T");
		  break;
		case SYS_DVBT2:
		  seq_puts(seq, "DVB-T2");
		  break;
		case SYS_DVBS:
		  seq_puts(seq, "DVB-S");
		  break;
		case SYS_DVBS2:
		  seq_puts(seq, "DVB-S2");
		  break;
		default:
		  seq_puts(seq, "undefined");
		  break;
	}
	seq_puts(seq, "\n");
}


static void satfreq2str(struct seq_file *seq, int frequency, enum fe_sec_tone_mode sectone)
{
	int freq = frequency / 100;
	if (sectone == SEC_TONE_ON)
	   freq += 106000;
	else
	  if (freq-97500 < 0)
	     freq += 97500;
	  else
	     freq -= 97500;

	seq_printf(seq, "  Frequency   : %i\n", freq/10);
}

static void fec2str(struct seq_file *seq, enum fe_code_rate fec)
{
	seq_puts(seq, "  FEC         : ");
	switch (fec) {
		case FEC_1_2:
		  seq_puts(seq, "1/2");
		  break;
		case FEC_2_3:
		  seq_puts(seq, "2/3");
		  break;
		case FEC_3_4:
		  seq_puts(seq, "3/4");
		  break;
		case FEC_4_5:
		  seq_puts(seq, "4/5");
		  break;
		case FEC_5_6:
		  seq_puts(seq, "5/6");
		  break;
		case FEC_6_7:
		  seq_puts(seq, "6/7");
		  break;
		case FEC_7_8:
		  seq_puts(seq, "7/8");
		  break;
		case FEC_8_9:
		  seq_puts(seq, "8/9");
		  break;
		case FEC_AUTO:
		  seq_puts(seq, "auto");
		  break;
		case FEC_3_5:
		  seq_puts(seq, "3/5");
		  break;
		case FEC_9_10:
		  seq_puts(seq, "9/10");
		  break;
		case FEC_2_5:
		  seq_puts(seq, "2/5");
		  break;
		default:
		  seq_puts(seq, "unknown");
		  break;
	}
	seq_puts(seq, "\n");
}

static void mod2str(struct seq_file *seq, enum fe_modulation modulation)
{
	seq_puts(seq, "  Modulation  : ");
	switch (modulation) {
		case QPSK:
		  seq_puts(seq, "QPSK");
		  break;
		case QAM_16:
		  seq_puts(seq, "QAM 16");
		  break;
		case QAM_32:
		  seq_puts(seq, "QAM 32");
		  break;
		case QAM_64:
		  seq_puts(seq, "QAM 64");
		  break;
		case QAM_128:
		  seq_puts(seq, "QAM 128");
		  break;
		case QAM_AUTO:
		  seq_puts(seq, "QUAM AUTO");
		  break;
		case PSK_8:
		  seq_puts(seq, "PSK 8");
		  break;
		case DQPSK:
		  seq_puts(seq, "DQPSK");
		  break;
		case QAM_4_NR:
		  seq_puts(seq, "QAM 4 NR");
		  break;
		default:
		  seq_puts(seq, "unknown"); 
	}
	seq_puts(seq, "\n");
}

static void roff2str(struct seq_file *seq, enum fe_rolloff rolloff)
{
	seq_puts(seq, "  Rolloff     : ");
	switch (rolloff) {
		case ROLLOFF_35:
		  seq_puts(seq, "0.35");
		  break;
		case ROLLOFF_20:
		  seq_puts(seq, "0.20");
		  break;
		case ROLLOFF_25:
		  seq_puts(seq, "0.25");
		  break;
		case ROLLOFF_AUTO:
		  seq_puts(seq, "auto");
		  break;
		default:
		  seq_puts(seq, "unknown");
		  break;
	}
	seq_puts(seq, "\n");
}

static void pilot2str(struct seq_file *seq, enum fe_pilot pilot)
{
	seq_puts(seq, "  Pilot       : ");
	switch (pilot) {
		case PILOT_ON:
		  seq_puts(seq, "on");
		  break;
		case PILOT_OFF:
		  seq_puts(seq, "off");
		  break;
		case PILOT_AUTO:
		  seq_puts(seq, "auto");
		  break;
		default:
		  seq_puts(seq, "unknown");
	}
	seq_puts(seq, "\n");
}

static int vtunerc_read_proc(struct seq_file *seq, void *v)
{
	int i, pcnt = 0;
	struct vtunerc_ctx *ctx = (struct vtunerc_ctx *)seq->private;

	seq_printf(seq, "[ vtunerc driver, version " VTUNERC_MODULE_VERSION " ]\n");
	seq_printf(seq, "  Used by     : %u\n", ctx->fd_opened);
	status2str(seq, ctx->signal.status);
	if (ctx->stat_time>0)
		seq_printf(seq, "  Last change : %lli\n", ktime_get_seconds()-ctx->stat_time);
	if (ctx->fe) {
		struct fe_params *fep = &ctx->fe_params;
		if (fep->frequency>0) {
			delsys2str(seq, fep->delivery_system);
			if (fep->delivery_system==SYS_DVBS || fep->delivery_system==SYS_DVBS2) {
				mod2str(seq, fep->u.qpsk.modulation);
				satfreq2str(seq, fep->frequency, fep->u.qpsk.sat.tone);
				seq_printf(seq, "  Symbolrate  : %i\n", fep->u.qpsk.symbol_rate / 1000);
				fec2str(seq, fep->u.qpsk.fec_inner);
				roff2str(seq, fep->u.qpsk.rolloff);
				pilot2str(seq, fep->u.qpsk.pilot);
			}
		}
	}
	seq_printf(seq, "  PID tab     :");
	for (i = 0; i < MAX_PIDTAB_LEN; i++)
		if (ctx->pidtab[i] != PID_UNKNOWN) {
			seq_printf(seq, " %i", ctx->pidtab[i]);
			if (ctx->pusitab[i]==1) seq_printf(seq, "*");
			if (ctx->pusitab[i]==2) seq_printf(seq, "-");
			pcnt++;
		}

	seq_printf(seq, " (len=%d)\n", pcnt);
	seq_printf(seq, "  TS data     : %lu\n", ctx->stat_wr_data);
	seq_printf(seq, "  Int. filler : %lu\n", ctx->stat_fi_data);
	seq_printf(seq, "  Ext. filler : %lu\n", ctx->stat_fe_data);
	return 0;
}

static int vtunerc_proc_open(struct inode *inode, struct file *file)
{
	int ret;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,17,0)
	struct vtunerc_ctx *ctx = PDE_DATA(inode);
#else
	struct vtunerc_ctx *ctx = pde_data(inode);
#endif

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;
	ret = single_open(file, vtunerc_read_proc, ctx);
	if (ret)
		module_put(THIS_MODULE);
	return ret;
}

static int vtuner_proc_release(struct inode *inode, struct file *file)
{
	int ret = single_release(inode, file);
	module_put(THIS_MODULE);
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
static const struct file_operations vtunerc_read_proc_fops = {
	.open		= vtunerc_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= vtuner_proc_release,
#else
static const struct proc_ops vtunerc_read_proc_ops = {
	.proc_open	= vtunerc_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= vtuner_proc_release,
#endif
	};

#endif

static char *my_strdup(const char *s)
{
	char *rv = kmalloc(strlen(s)+1, GFP_KERNEL);
	if (rv)
		strcpy(rv, s);
	return rv;
}

struct vtunerc_ctx *vtunerc_get_ctx(int minor)
{
	if (minor >= VTUNERC_MAX_ADAPTERS)
		return NULL;

	return vtunerc_tbl[minor];
}

static int __init vtunerc_init(void)
{
	struct vtunerc_ctx *ctx = NULL;
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int ret = -EINVAL, i, idx;

	printk(KERN_INFO "virtual DVB adapter driver, version " VTUNERC_MODULE_VERSION ", (c) 2021 Honza Petrous, SmartImp.cz\n");

	request_module("dvb-core");

	for (idx = 0; idx < config.devices; idx++) {
		ctx = kzalloc(sizeof(struct vtunerc_ctx), GFP_KERNEL);
		if (!ctx) {
			while(idx)
				kfree(vtunerc_tbl[--idx]);
			return -ENOMEM;
		}

		vtunerc_tbl[idx] = ctx;

		ctx->idx = idx;
		ctx->config = &config;
		ctx->ctrldev_request.type = -1;
		ctx->ctrldev_response.type = -1;
		init_waitqueue_head(&ctx->ctrldev_wait_request_wq);
		init_waitqueue_head(&ctx->ctrldev_wait_response_wq);
		init_waitqueue_head(&ctx->ctrldev_wait_packet_wq);

		// buffer
		ctx->kernel_buf = NULL;
		ctx->kernel_buf_size = 0;

		/* dvb */

		/* create new adapter */
		ret = dvb_register_adapter(&ctx->dvb_adapter, DRIVER_NAME, THIS_MODULE, NULL, adapter_nr);
		if (ret < 0)
			goto err_kfree;

		ctx->dvb_adapter.priv = ctx;

		memset(&ctx->demux, 0, sizeof(ctx->demux));
		dvbdemux = &ctx->demux;
		dvbdemux->priv = ctx;
		dvbdemux->filternum = MAX_PIDTAB_LEN;
		dvbdemux->feednum = MAX_PIDTAB_LEN;
		dvbdemux->start_feed = vtunerc_start_feed;
		dvbdemux->stop_feed = vtunerc_stop_feed;
		dvbdemux->dmx.capabilities = 0;
		ret = dvb_dmx_init(dvbdemux);
		if (ret < 0)
			goto err_dvb_unregister_adapter;

		dmx = &dvbdemux->dmx;

		ctx->hw_frontend.source = DMX_FRONTEND_0;
		ctx->mem_frontend.source = DMX_MEMORY_FE;
		ctx->dmxdev.filternum = MAX_PIDTAB_LEN;
		ctx->dmxdev.demux = dmx;

		ret = dvb_dmxdev_init(&ctx->dmxdev, &ctx->dvb_adapter);
		if (ret < 0)
			goto err_dvb_dmx_release;

		ret = dmx->add_frontend(dmx, &ctx->hw_frontend);
		if (ret < 0)
			goto err_dvb_dmxdev_release;

		ret = dmx->add_frontend(dmx, &ctx->mem_frontend);
		if (ret < 0)
			goto err_remove_hw_frontend;

		ret = dmx->connect_frontend(dmx, &ctx->hw_frontend);
		if (ret < 0)
			goto err_remove_mem_frontend;

		vtunerc_frontend_init(ctx);

		sema_init(&ctx->xchange_sem, 1);
		sema_init(&ctx->ioctl_sem, 1);
		sema_init(&ctx->tswrite_sem, 1);

		/* init pid table */
		for (i = 0; i < MAX_PIDTAB_LEN; i++)
			ctx->pidtab[i] = PID_UNKNOWN;

#ifdef CONFIG_PROC_FS
		{
			char procfilename[64];

			sprintf(procfilename, VTUNERC_PROC_FILENAME,
					ctx->idx);
			ctx->procname = my_strdup(procfilename);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
			if (proc_create_data(ctx->procname, 0, NULL, &vtunerc_read_proc_fops, ctx) == 0)
#else
			if (proc_create_data(ctx->procname, 0, NULL, &vtunerc_read_proc_ops, ctx) == 0)
#endif
				printk(KERN_WARNING "vtunerc%d: Unable to register '%s' proc file\n", ctx->idx, ctx->procname);

		}
#endif
	}

	vtunerc_register_ctrldev(ctx);

out:
	return ret;

	dmx->disconnect_frontend(dmx);
err_remove_mem_frontend:
	dmx->remove_frontend(dmx, &ctx->mem_frontend);
err_remove_hw_frontend:
	dmx->remove_frontend(dmx, &ctx->hw_frontend);
err_dvb_dmxdev_release:
	dvb_dmxdev_release(&ctx->dmxdev);
err_dvb_dmx_release:
	dvb_dmx_release(dvbdemux);
err_dvb_unregister_adapter:
	dvb_unregister_adapter(&ctx->dvb_adapter);
err_kfree:
	kfree(ctx);
	goto out;
}

static void __exit vtunerc_exit(void)
{
	struct dvb_demux *dvbdemux;
	struct dmx_demux *dmx;
	int idx;

	vtunerc_unregister_ctrldev(&config);

	for (idx = 0; idx < config.devices; idx++) {
		struct vtunerc_ctx *ctx = vtunerc_tbl[idx];
		if(!ctx)
			continue;
		vtunerc_tbl[idx] = NULL;
#ifdef CONFIG_PROC_FS
		remove_proc_entry(ctx->procname, NULL);
		kfree(ctx->procname);
#endif

		vtunerc_frontend_clear(ctx);

		dvbdemux = &ctx->demux;
		dmx = &dvbdemux->dmx;

		dmx->disconnect_frontend(dmx);
		dmx->remove_frontend(dmx, &ctx->mem_frontend);
		dmx->remove_frontend(dmx, &ctx->hw_frontend);
		dvb_dmxdev_release(&ctx->dmxdev);
		dvb_dmx_release(dvbdemux);
		dvb_unregister_adapter(&ctx->dvb_adapter);

		// free allocated buffer
		if(ctx->kernel_buf != NULL) {
			kfree(ctx->kernel_buf);
			printk(KERN_INFO "vtunerc%d: deallocated buffer of %zu bytes\n", idx, ctx->kernel_buf_size);
			ctx->kernel_buf = NULL;
			ctx->kernel_buf_size = 0;

		}

		kfree(ctx);
	}

	printk(KERN_NOTICE "vtunerc: unloaded successfully\n");
}

module_init(vtunerc_init);
module_exit(vtunerc_exit);

MODULE_AUTHOR("Honza Petrous");
MODULE_DESCRIPTION("virtual DVB device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VTUNERC_MODULE_VERSION);

module_param_named(devices, config.devices, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(devices, "Number of virtual adapters (default is 1)");

module_param_named(debug, config.debug, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(debug, "Enable debug messages (default is 0)");
