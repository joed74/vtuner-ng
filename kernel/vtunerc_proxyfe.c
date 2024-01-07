/*
 * vtunerc: Driver for Proxy Frontend
 *
 * Copyright (C) 2010-12 Honza Petrous <jpetrous@smartimp.cz>
 * [Inspired on proxy frontend by Emard <emard@softhome.net>]
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "vtunerc_priv.h"

#include <media/dvb_frontend.h>

#if (DVB_API_VERSION << 8 | DVB_API_VERSION_MINOR) < 0x0505
 #error ========================================================================
 #error Version 5.5 or newer of DVB API is required (see at linux/dvb/version.h)
 #error You can find it in kernel version >= 3.3.0
 #error ========================================================================
#endif

struct dvb_proxyfe_state {
	struct dvb_frontend frontend;
	struct vtunerc_ctx *ctx;
};

void dvb_proxyfe_set_stat(struct vtuner_dtv_fe_stats *vtuner_stats, struct dtv_fe_stats *dvb_stats)
{
	int i;
	if (vtuner_stats->len > MAX_DTV_STATS) vtuner_stats->len=MAX_DTV_STATS;
	dvb_stats->len = vtuner_stats->len;
	for (i=0; i < vtuner_stats->len; i++) {
		dvb_stats->stat[i].scale = vtuner_stats->stat[i].scale;
		if (dvb_stats->stat[i].scale == FE_SCALE_DECIBEL) {
			dvb_stats->stat[i].svalue = vtuner_stats->stat[i].u.svalue;
		} else {
			dvb_stats->stat[i].uvalue = vtuner_stats->stat[i].u.uvalue;
		}
	}
}

void dvb_proxyfe_set_signal(struct vtunerc_ctx *ctx)
{
	struct dvb_frontend *fe = ctx ? ctx->fe : NULL;
	struct dtv_frontend_properties *c = fe ? &fe->dtv_property_cache : NULL;
	if (!c) return;

	dvb_proxyfe_set_stat(&ctx->signal.strength, &c->strength);
	dvb_proxyfe_set_stat(&ctx->signal.cnr, &c->cnr);
	dvb_proxyfe_set_stat(&ctx->signal.pre_bit_error, &c->pre_bit_error);
	dvb_proxyfe_set_stat(&ctx->signal.pre_bit_count, &c->pre_bit_count);
	dvb_proxyfe_set_stat(&ctx->signal.post_bit_error, &c->post_bit_error);
	dvb_proxyfe_set_stat(&ctx->signal.post_bit_count, &c->post_bit_count);
	dvb_proxyfe_set_stat(&ctx->signal.block_error, &c->block_error);
	dvb_proxyfe_set_stat(&ctx->signal.block_count, &c->block_count);
}

static int dvb_proxyfe_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	*status = ctx->signal.status;
	return 0;
}

static int dvb_proxyfe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	//struct dvb_proxyfe_state *state = fe->demodulator_priv;
	//struct vtunerc_ctx *ctx = state->ctx;

	*ber = 0;
	return 0;
}

static int dvb_proxyfe_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	int i;
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	*strength=0;
	for (i=0; i<ctx->signal.strength.len; i++) {
		if (ctx->signal.strength.stat[i].scale == FE_SCALE_RELATIVE) {
			*strength = ctx->signal.strength.stat[i].u.uvalue;
		}
	}
	return 0;
}

static int dvb_proxyfe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	int i;
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	*snr=0;
	for (i=0; i<ctx->signal.cnr.len; i++) {
		if (ctx->signal.cnr.stat[i].scale == FE_SCALE_RELATIVE) {
			*snr = ctx->signal.cnr.stat[i].u.uvalue;
		}
	}
	return 0;
}

static int dvb_proxyfe_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	//struct dvb_proxyfe_state *state = fe->demodulator_priv;
	//struct vtunerc_ctx *ctx = state->ctx;

	*ucblocks = 0;
	return 0;
}

static int dvb_proxyfe_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	struct vtuner_message msg;

	if (ctx->fd_opened < 1) return -EAGAIN;
	if (c->frequency == 0) return -EINVAL;

	memset(&msg, 0, sizeof(msg));
	msg.body.fe_params.delivery_system = c->delivery_system;
	msg.body.fe_params.frequency = c->frequency;

	switch (c->delivery_system) {
	case SYS_DVBS:
	case SYS_DVBS2:
		msg.body.fe_params.u.qpsk.symbol_rate = c->symbol_rate;
		msg.body.fe_params.u.qpsk.fec_inner = c->fec_inner;
		msg.body.fe_params.u.qpsk.modulation = c->modulation;
		msg.body.fe_params.u.qpsk.pilot = c->pilot;
		msg.body.fe_params.u.qpsk.rolloff = c->rolloff;
		memcpy(&msg.body.fe_params.u.qpsk.sat, &ctx->fe_params.u.qpsk.sat, sizeof(struct sat_params));
		break;
	case SYS_DVBT:
	case SYS_DVBT2:
		msg.body.fe_params.u.ofdm.bandwidth = c->bandwidth_hz;
		msg.body.fe_params.u.ofdm.code_rate_HP = c->code_rate_HP;
		msg.body.fe_params.u.ofdm.code_rate_LP = c->code_rate_LP;
		msg.body.fe_params.u.ofdm.constellation = c->modulation;
		msg.body.fe_params.u.ofdm.transmission_mode = c->transmission_mode;
		msg.body.fe_params.u.ofdm.guard_interval = c->guard_interval;
		msg.body.fe_params.u.ofdm.hierarchy_information = c->hierarchy;
		break;
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_B:
	case SYS_DVBC_ANNEX_C:
		msg.body.fe_params.u.qam.inversion = c->inversion;
		msg.body.fe_params.u.qam.symbol_rate = c->symbol_rate;
		msg.body.fe_params.u.qam.modulation = c->modulation;
		break;
	default:
		printk(KERN_ERR "vtunerc%d: unregognized tuner type = %d\n", ctx->idx, c->delivery_system);
		return -EINVAL;
	}

	if (memcmp(&msg.body.fe_params, &ctx->fe_params, sizeof(struct fe_params))!=0) {

		ctx->stat_time = ktime_get_seconds();
		ctx->signal.status = FE_NONE;

		dprintk(ctx, "MSG_SET_FRONTEND, set signal NONE\n");

		msg.type = MSG_SET_FRONTEND;
		vtunerc_ctrldev_xchange_message(ctx, &msg, 1);
		memcpy(&ctx->fe_params, &msg.body.fe_params, sizeof(struct fe_params));

		send_pidlist(ctx);
	}
	return 0;
}

static int dvb_proxyfe_tune(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, enum fe_status *status)
{
	return dvb_proxyfe_set_frontend(fe);
}

static enum dvbfe_algo dvb_proxyfe_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int dvb_proxyfe_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static int dvb_proxyfe_init(struct dvb_frontend *fe)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	dprintk(ctx, "init\n");
	ctx->adapter_inuse=1;
	return 0;
}

static void dvb_proxyfe_detach(struct dvb_frontend *fe)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;
	dprintk(ctx, "detach\n");
	ctx->adapter_inuse=0;
}

static int dvb_proxyfe_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	ctx->fe_params.u.qpsk.sat.tone = tone;
	return 0;
}

static int dvb_proxyfe_set_voltage(struct dvb_frontend *fe, enum fe_sec_voltage voltage)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	ctx->fe_params.u.qpsk.sat.voltage = voltage;
	return 0;
}

static int dvb_proxyfe_send_diseqc_msg(struct dvb_frontend *fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	memcpy(&ctx->fe_params.u.qpsk.sat.diseqc_master_cmd, cmd, sizeof(struct dvb_diseqc_master_cmd));
	return 0;
}

static int dvb_proxyfe_send_diseqc_burst(struct dvb_frontend *fe, enum fe_sec_mini_cmd burst)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	struct vtunerc_ctx *ctx = state->ctx;

	ctx->fe_params.u.qpsk.sat.burst = burst;
	return 0;
}

static void dvb_proxyfe_release(struct dvb_frontend *fe)
{
	struct dvb_proxyfe_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops dvb_proxyfe_ops;

static struct dvb_frontend *dvb_proxyfe_attach(struct vtunerc_ctx *ctx)
{
	struct dvb_frontend *fe = ctx->fe;

	if (!fe) {
		struct dvb_proxyfe_state *state = NULL;

		/* allocate memory for the internal state */
		state = kmalloc(sizeof(struct dvb_proxyfe_state), GFP_KERNEL);
		if (state == NULL) {
			return NULL;
		}

		fe = &state->frontend;
		fe->demodulator_priv = state;
		fe->id = 0;
		state->ctx = ctx;
	}

	memcpy(&fe->ops, &dvb_proxyfe_ops, sizeof(struct dvb_frontend_ops));

	return fe;
}

static struct dvb_frontend_ops dvb_proxyfe_ops = {
        .delsys = { SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A, SYS_DVBC_ANNEX_B, SYS_DVBC_ANNEX_C, SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name			= "vTuner proxyFE DVB-Multi",
		.frequency_min_hz	= 51 * MHz,
		.frequency_max_hz	= 2150 * MHz,
		.frequency_stepsize_hz	= 62.5 * kHz,
		.frequency_tolerance_hz	= 29500 * kHz,
		.symbol_rate_min	= 450000,
		.symbol_rate_max	= 45000000,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_4_5 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_QPSK | FE_CAN_RECOVER |
			FE_CAN_FEC_AUTO | FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 | FE_CAN_QAM_128 | FE_CAN_QAM_256 | 
			FE_CAN_QAM_AUTO | FE_CAN_8VSB | FE_CAN_16VSB | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO | FE_CAN_FEC_AUTO |
			FE_CAN_INVERSION_AUTO | FE_CAN_2G_MODULATION | FE_CAN_TURBO_FEC | FE_CAN_MULTISTREAM
	},

	.release = dvb_proxyfe_release,

	.init = dvb_proxyfe_init,
	.detach = dvb_proxyfe_detach,
	.sleep = dvb_proxyfe_sleep,

	.get_frontend_algo = dvb_proxyfe_get_frontend_algo,
	.set_frontend = dvb_proxyfe_set_frontend,
	.tune = dvb_proxyfe_tune,

	.read_status = dvb_proxyfe_read_status,
        .read_ber = dvb_proxyfe_read_ber,
        .read_signal_strength = dvb_proxyfe_read_signal_strength,
        .read_snr = dvb_proxyfe_read_snr,
        .read_ucblocks = dvb_proxyfe_read_ucblocks,


	.set_voltage = dvb_proxyfe_set_voltage,
	.set_tone = dvb_proxyfe_set_tone,

	.diseqc_send_master_cmd = dvb_proxyfe_send_diseqc_msg,
	.diseqc_send_burst = dvb_proxyfe_send_diseqc_burst,
};

int /*__devinit*/ vtunerc_frontend_init(struct vtunerc_ctx *ctx)
{
	if (ctx->fe) {
		printk(KERN_NOTICE "vtunerc%d: frontend already initialized\n", ctx->idx);
		return 0;
	}

	ctx->fe = dvb_proxyfe_attach(ctx);
	return dvb_register_frontend(&ctx->dvb_adapter, ctx->fe);
}

int /*__devinit*/ vtunerc_frontend_clear(struct vtunerc_ctx *ctx)
{
	if (ctx->fe) {
		dvb_unregister_frontend(ctx->fe);
		kfree(ctx->fe->demodulator_priv);
		ctx->fe = NULL;
	}
	return 0;
}
