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

#define T_SB	       0x80
#define T_CREATE_TC    0x82
#define T_CTC_REPLY    0x83
#define T_DATA_LAST    0xA0

#define ST_SESSION_NUMBER           0x90
#define ST_OPEN_SESSION_REQUEST     0x91

#define AOT_CA_INFO_ENQ			0x9F8030
#define AOT_CA_INFO			0x9F8031
#define AOT_CA_PMT			0x9F8032
#define AOT_CA_PMT_REPLY		0x9F8033
#define AOT_APPLICATION_INFO_ENQ	0x9F8020

#define RI_APPLICATION_INFORMATION 	0x00020041
#define RI_CONDITIONAL_ACCESS_SUPPORT	0x00030041

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
	u8 data_length;
	u8 data_mem[255];
	bool ca_session;
	bool ai_session;
};

struct vtunerc_ca_private {
	struct vtunerc_ctx *ctx;
	unsigned int slot_count;
	struct vtunerc_ca_slot *slot_info;
};

struct vtunerc_tpdu {
	u8 slot;
	u8 tcid;
	u8 tag;
};

struct vtunerc_spdu {
	u8 length;
	u8 tcid;
	u8 tag;
	u8 field_length;
	u16 session_number;
	u8 aot0;
	u8 aot1;
	u8 aot2;
	u8 aot_length;
} __attribute__ ((packed));

int vtunerc_ca_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	//printk(KERN_INFO "ca_read_attribute_mem slot=%i address=%i myaddress=%i\n", slot, address, myaddress);
	if (myaddress<0) return 0xFF;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	return sl->tuple_mem[myaddress];
}

int vtunerc_ca_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	//printk(KERN_INFO "ca_write_attribute_mem slot=%i address=%i value=%i\n", slot, address, value);
	if (myaddress<0) return -EIO;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	sl->tuple_mem[myaddress]=value;
	return 0;
}

int vtunerc_ca_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct vtunerc_ca_private *priv = ca->data;
	struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	//printk(KERN_INFO "ca_read_cam_control slot=%i address=%i\n", slot, address);
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

	//printk(KERN_INFO "ca_write_cam_control slot=%i address=%i value=%i\n",slot, address, value);
	if (address==CTRLIF_COMMAND) {
	    if (value == (IRQEN|CMDREG_SR)) sl->nextstatus=STATUSREG_DA;
	    if (value == (IRQEN|CMDREG_SW)) sl->nextstatus=STATUSREG_FR;
	}
	return 0;
}

int vtunerc_ca_send_ctc_reply(u8 *buf, u8 slot, u8 tcid)
{
	buf[0]=slot;
	buf[1]=tcid;
	buf[2]=T_CTC_REPLY;
	buf[3]=0x01;
	buf[4]=tcid;
	return 5;
}

int vtunerc_ca_send_sb_reply(u8 *buf, u8 slot, u8 tcid)
{
	buf[0]=slot;
	buf[1]=tcid;
	buf[2]=T_SB;
	buf[3]=0x01;
	buf[4]=tcid;
	return 5;
}

int vtunerc_ca_send_session_request(u8 *buf, u8 slot, u8 tcid, u32 resource_id)
{
	u32 swapped_id = cpu_to_be32(resource_id);

	buf[0]=slot;
	buf[1]=tcid;
	buf[2]=T_DATA_LAST;
	buf[3]=0x07;
	buf[4]=tcid;
	buf[5]=ST_OPEN_SESSION_REQUEST;
	buf[6]=0x04;
	memcpy(&buf[7],&swapped_id,4);
	return 11;
}

int vtunerc_ca_send_ca_info(u8 *ebuf, struct vtunerc_ca_slot *sl)
{
	// copy most of request
	memcpy(ebuf, sl->data_mem, 12);
	ebuf[11]=0x31; // AOT_CA_INFO
	ebuf[12]=0x02;
	ebuf[13]=0x22; // CA-ID
	ebuf[14]=0x23; // CA-ID
	return 15;
}

int vtunerc_ca_send_pmt_reply(u8 *ebuf, struct vtunerc_ca_slot *sl)
{
	// copy most of request
	memcpy(ebuf, sl->data_mem, 12);
	ebuf[11]=0x33; // AOT_CA_PMT_REPLY
	ebuf[12]=0x04; // length field
	ebuf[13]=sl->data_mem[14]; // program_number
	ebuf[14]=sl->data_mem[15];
	ebuf[15]=sl->data_mem[16]; // version_number + current_next_indicator
	ebuf[16]=0x81; // "descrambling possible"
	return 17;
}

int vtunerc_ca_send_app_info(u8 *ebuf, struct vtunerc_ca_slot *sl)
{
	// copy most of request
	memcpy(ebuf, sl->data_mem, 12);
	ebuf[11]=0x21; // AOT_APPLICATION_INFO
	ebuf[12]=0x0f; // length field
	ebuf[13]=0x01;
	ebuf[14]=0x00;
	ebuf[15]=0x01;
	ebuf[16]=0x08;
	ebuf[17]=0x15;
	ebuf[18]=0x09;
	ebuf[19]='v';
	ebuf[20]='t';
	ebuf[21]='u';
	ebuf[22]='n';
	ebuf[23]='e';
	ebuf[24]='r';
	ebuf[25]='-';
	ebuf[26]='n';
	ebuf[27]='g';
	return 28;
}

int vtunerc_ca_read_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];
	struct vtunerc_tpdu *tpdu;
	struct vtunerc_spdu *spdu;

	if (!sl->data_length) return 0;
	ecount=0;

	tpdu = (void *) sl->data_mem; // last data received
	if (tpdu->tag==T_CREATE_TC) {
		ecount=vtunerc_ca_send_ctc_reply(ebuf, tpdu->slot, tpdu->tcid);
	} else if (tpdu->tag==T_DATA_LAST) {
		// now answer some requests
		if (sl->data_length<=6) {
			if (sl->ai_session && !sl->ca_session) {
			       	ecount=vtunerc_ca_send_session_request(ebuf, tpdu->slot, tpdu->tcid, RI_CONDITIONAL_ACCESS_SUPPORT);
				sl->ca_session=true;
			} else if (!sl->ai_session) {
				ecount=vtunerc_ca_send_session_request(ebuf, tpdu->slot, tpdu->tcid, RI_APPLICATION_INFORMATION);
				sl->ai_session=true;
			} else {
				ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
			}
		} else {
			spdu = (void *) &sl->data_mem[3];
			if (spdu->tag == ST_SESSION_NUMBER) {
				int aot = spdu->aot0<<16 | spdu->aot1<<8 | spdu->aot2;
				switch (aot) {
					case AOT_CA_INFO_ENQ:
						ecount=vtunerc_ca_send_ca_info(ebuf, sl);
						break;
					case AOT_CA_PMT:
						ecount=vtunerc_ca_send_pmt_reply(ebuf, sl);
						break;
					case AOT_APPLICATION_INFO_ENQ:
						ecount=vtunerc_ca_send_app_info(ebuf, sl);
						break;
				}
		     }
	     }
	}
	sl->data_length=0; // next read return zero
	if (ecount<=30 && ebuf[2]!=T_SB) print_hex_dump_bytes("ca_read_data  ", DUMP_PREFIX_NONE, ebuf, ecount);
	return ecount;
}

int vtunerc_ca_write_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	if (ecount>255) return -EINVAL;
	memcpy(&sl->data_mem,ebuf,ecount);
	sl->data_length=ecount;

	if (ecount<=30) {
		if (ebuf[2]==T_DATA_LAST && ebuf[3]==1) return ecount;
		print_hex_dump_bytes("ca_write_data ", DUMP_PREFIX_NONE, ebuf, ecount);
	}
	return ecount;
}

int vtunerc_ca_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	dvb_ca_en50221_camready_irq(ca, slot);
	return 0;
}

int vtunerc_ca_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	sl->ca_session=false;
	sl->ai_session=false;
	sl->data_length=0;
	sl->nextstatus=STATUSREG_DA|STATUSREG_FR;
	return 0;
}

int vtunerc_ca_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	// return code not evaluated
	return 0;
}

int vtunerc_ca_insert(struct vtunerc_ctx *ctx, int slot)
{
	dvb_ca_en50221_camchange_irq(&ctx->pubca, slot, DVB_CA_EN50221_CAMCHANGE_INSERTED);
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
	ret = dvb_ca_en50221_init(&ctx->dvb_adapter, &ctx->pubca,
			DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE | 
			DVB_CA_EN50221_FLAG_IRQ_FR | 
			DVB_CA_EN50221_FLAG_IRQ_DA, slot_count);

	for (i=0; i < slot_count; i++) {
		dvb_ca_en50221_frda_irq(&ctx->pubca, i); // enable "irq"
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
