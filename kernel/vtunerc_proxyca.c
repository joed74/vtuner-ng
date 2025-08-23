/*
 * vtunerc: Driver for Proxy Frontend
 *
 * Copyright (C) 2025 Jochen Dolze
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

#include "vtunerc_priv.h"

#define CTRLIF_DATA 0
#define CTRLIF_COMMAND 1
#define CTRLIF_STATUS 1
#define CTRLIF_SIZE_LOW 2
#define CTRLIF_SIZE_HIGH 3

#define CMDREG_SW        2	/* Size write */
#define CMDREG_SR        4	/* Size read */
#define CMDREG_DAIE   0x80	/* Enable DA interrupt */
#define IRQEN (CMDREG_DAIE)

#define STATUSREG_FR  0x40	/* module free */
#define STATUSREG_DA  0x80	/* data available */

#define DVB_CA_SLOTSTATE_NONE           0
#define DVB_CA_SLOTSTATE_UNINITIALISED  1
#define DVB_CA_SLOTSTATE_RUNNING        2
#define DVB_CA_SLOTSTATE_INVALID        3
#define DVB_CA_SLOTSTATE_WAITREADY      4
#define DVB_CA_SLOTSTATE_VALIDATE       5
#define DVB_CA_SLOTSTATE_WAITFR         6
#define DVB_CA_SLOTSTATE_LINKINIT       7

const u8 tuple_mem[128]={ 0x1d,0x04,0x02,0x08,0x00,0xff, // CISTPL_DEVICE_0A
		    0x1c,0x04,0x02,0x08,0x00,0xff, // CISTPL_DEVICE_0C
		    0x15,0x0f,0x05,0x00,0x09,0x76,0x74,0x75,0x6e,0x65,0x72,0x2d,0x6e,0x67,0x00,0x00,0xff, // CISTPL_VER_1
		    0x20,0x04,0x15,0x08,0x15,0x08, // CISTPL_MANFID
		    0x1a,0x17,0x00,0x0f,0xfe,0x01,0x01,0xc0,0x0e,0x41,0x02,0x44,0x56,0x42,0x5f,0x43,0x49, // CISTPL_CONFIG
		    0x5f,0x56,0x31,0x2e,0x30,0x30,0x00,0xff,
		    0x1b,0x26,0xcf,0x04,0x09,0x37,0x55,0x4d,0x5d,0x1d,0x56,0x22,0xc0,0x09,0x44,0x56,0x42, // CISTPL_CFTABLE_ENTRY
		    0x5f,0x48,0x4f,0x53,0x54,0x00,0xc1,0x0e,0x44,0x56,0x42,0x5f,0x43,0x49,0x5f,0x4d,0x4f,
		    0x44,0x55,0x4c,0x45,0x00,0xff,
		    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, // just filler
		    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};

struct vtunerc_ca_slot {
	u8 nextstatus;
	u8 tuple_mem[128];
};

struct vtunerc_ca_private {
	struct vtunerc_ctx *ctx;
	unsigned int slot_count;
	struct vtunerc_ca_slot *slot_info;
};

int vtunerc_ca_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	printk(KERN_INFO "ca_read_attribute_mem slot=%i address=%i myaddress=%i\n", slot, address, myaddress);
	if (myaddress<0) return 0xFF;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	return sl->tuple_mem[myaddress];
}

int vtunerc_ca_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	printk(KERN_INFO "ca_write_attribute_mem slot=%i address=%i value=%i\n", slot, address, value);
	if (myaddress<0) return -EIO;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	sl->tuple_mem[myaddress]=value;
	return 0;
}

int vtunerc_ca_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct vtunerc_ca_private *priv = ca->data;
	struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	printk(KERN_INFO "ca_read_cam_control slot=%i address=%i\n", slot, address);
	if (address==CTRLIF_STATUS) return sl->nextstatus;
	if (address==CTRLIF_SIZE_HIGH) return 0;
	if (address==CTRLIF_SIZE_LOW) return 2; // 2 bytes
	if (address==CTRLIF_DATA) return 0xff; // "data"
	return -EIO;
}

int vtunerc_ca_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	printk(KERN_INFO "ca_write_cam_control slot=%i address=%i value=%i\n",slot, address, value);
	if (address==CTRLIF_COMMAND) {
	    if (value == (IRQEN|CMDREG_SR)) sl->nextstatus=STATUSREG_DA;
	    if (value == (IRQEN|CMDREG_SW)) sl->nextstatus=STATUSREG_FR;
	}
	return 0;
}

int vtunerc_ca_read_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	printk(KERN_INFO "ca_read_data slot=%i data=%p count=%i\n", slot, ebuf, ecount);
	return -EIO;
}

int vtunerc_ca_write_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	printk(KERN_INFO "ca_write_data slot=%i data=%p count=%i\n", slot, ebuf, ecount);
	return -EIO;
}

int vtunerc_ca_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	return 0;
}

int vtunerc_ca_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	return 0;
}

int vtunerc_ca_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	// return code not evaluated
	return 0;
}

int vtunerc_ca_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	return DVB_CA_EN50221_POLL_CAM_READY;
}

int vtunerc_ca_insert(struct vtunerc_ctx *ctx, int slot)
{
	dvb_ca_en50221_camchange_irq(&ctx->pubca, slot, DVB_CA_EN50221_CAMCHANGE_INSERTED);
	mdelay(200);
	dvb_ca_en50221_camready_irq(&ctx->pubca, slot);
	return 0;
}

int vtunerc_ca_remove(struct vtunerc_ctx *ctx, int slot)
{
	dvb_ca_en50221_camchange_irq(&ctx->pubca, slot, DVB_CA_EN50221_CAMCHANGE_REMOVED);
	return 0;
}

int vtunerc_ca_init(struct vtunerc_ctx *ctx, int slot_count)
{
	int ret;
	int i;
	struct vtunerc_ca_private *ca = NULL;

	ca = kzalloc(sizeof(*ca), GFP_KERNEL);
	if (!ca) return -ENOMEM;

	ca->ctx = ctx;
	ca->slot_count = slot_count;
	ca->slot_info = kcalloc(slot_count, sizeof(struct vtunerc_ca_slot), GFP_KERNEL);
	if (!ca->slot_info) {
		kfree(ca);
		return -ENOMEM;
	}

	/* now initialize each slot */
	for (i=0; i< slot_count; i++) {
		struct vtunerc_ca_slot *sl = &ca->slot_info[i];
		sl->nextstatus=0xC0;
		memcpy(sl->tuple_mem,&tuple_mem,sizeof(tuple_mem));
	}
	ctx->pubca.data = ca;

	ctx->pubca.owner = THIS_MODULE;
	ctx->pubca.read_attribute_mem = vtunerc_ca_read_attribute_mem;
	ctx->pubca.write_attribute_mem = vtunerc_ca_write_attribute_mem;
	ctx->pubca.read_cam_control = vtunerc_ca_read_cam_control;
	ctx->pubca.write_cam_control = vtunerc_ca_write_cam_control;
	ctx->pubca.read_data = vtunerc_ca_read_data;
	ctx->pubca.write_data = vtunerc_ca_write_data;
	ctx->pubca.slot_reset = vtunerc_ca_slot_reset;
	ctx->pubca.slot_shutdown = vtunerc_ca_slot_shutdown;
	ctx->pubca.slot_ts_enable = vtunerc_ca_slot_ts_enable;
	ctx->pubca.poll_slot_status = vtunerc_ca_poll_slot_status;
	ret = dvb_ca_en50221_init(&ctx->dvb_adapter, &ctx->pubca,
			DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE | 
			DVB_CA_EN50221_FLAG_IRQ_FR | 
			DVB_CA_EN50221_FLAG_IRQ_DA, slot_count);

	for (i=0; i < slot_count; i++) {
		dvb_ca_en50221_frda_irq(&ctx->pubca, i);
		vtunerc_ca_insert(ctx, i);
	}
	return ret;
}

int vtunerc_ca_clear(struct vtunerc_ctx *ctx)
{
	if (!ctx) return -EINVAL;
	if (ctx->pubca.private!=NULL) dvb_ca_en50221_release(&ctx->pubca);
	ctx->pubca.private=NULL;
	if (ctx->pubca.data) {
		struct vtunerc_ca_private *ca = ctx->pubca.data;
		kfree(ca->slot_info);
		kfree(ca);
		ctx->pubca.data=NULL;
	}
	return 0;
}
