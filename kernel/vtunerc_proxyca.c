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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <media/dvb_ringbuffer.h>

#include "vtunerc_priv.h"

#define BUFFER_SIZE 8192
#define PKT_READY 0
#define PKT_DISPOSED 1

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

#define RI_APPLICATION_INFORMATION 		0x00020041
#define RI_CONDITIONAL_ACCESS_SUPPORT		0x00030041
#define RI_CONDITIONAL_ACCESS_SUPPORT_CIPLUS	0x00030081

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
	bool ca_session;
	bool ai_session;
	bool can_decrypt;
	u8 nextstatus;
	u8 tuple_mem[128];
	struct dvb_ringbuffer rbuf;
	struct vtunerc_cainfo info;
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
	u8 tcid;
	u8 tag;
	u8 field_length;
	u16 session_number;
	u8 aot0;
	u8 aot1;
	u8 aot2;
} __attribute__ ((packed));

struct vtunerc_pmt {
	u8 list_management;
	u16 program_number;
	u8 version_next_indicator;
	u16 program_info_length;
	u8 ca_pmt_cmd_id;
} __attribute__ ((packed));

ssize_t dvb_ringbuffer_pkt_write(struct dvb_ringbuffer *rbuf, u8* buf, size_t len)
{
	int status;
	ssize_t oldpwrite = rbuf->pwrite;

	DVB_RINGBUFFER_WRITE_BYTE(rbuf, len >> 8);
	DVB_RINGBUFFER_WRITE_BYTE(rbuf, len & 0xff);
	DVB_RINGBUFFER_WRITE_BYTE(rbuf, PKT_READY);
	status = dvb_ringbuffer_write(rbuf, buf, len);

	if (status < 0) rbuf->pwrite = oldpwrite;
	return status;
}

ssize_t dvb_ringbuffer_pkt_next(struct dvb_ringbuffer *rbuf, size_t idx, size_t* pktlen)
{
	int consumed;
	int curpktlen;
	int curpktstatus;

	if (idx == -1) {
	       idx = rbuf->pread;
	} else {
		curpktlen = rbuf->data[idx] << 8;
		curpktlen |= rbuf->data[(idx + 1) % rbuf->size];
		idx = (idx + curpktlen + DVB_RINGBUFFER_PKTHDRSIZE) % rbuf->size;
	}

	consumed = (idx - rbuf->pread) % rbuf->size;

	while((dvb_ringbuffer_avail(rbuf) - consumed) > DVB_RINGBUFFER_PKTHDRSIZE) {

		curpktlen = rbuf->data[idx] << 8;
		curpktlen |= rbuf->data[(idx + 1) % rbuf->size];
		curpktstatus = rbuf->data[(idx + 2) % rbuf->size];

		if (curpktstatus == PKT_READY) {
			*pktlen = curpktlen;
			return idx;
		}

		consumed += curpktlen + DVB_RINGBUFFER_PKTHDRSIZE;
		idx = (idx + curpktlen + DVB_RINGBUFFER_PKTHDRSIZE) % rbuf->size;
	}

	// no packets available
	return -1;
}

ssize_t dvb_ringbuffer_pkt_read(struct dvb_ringbuffer *rbuf, size_t idx,
				int offset, u8* buf, size_t len)
{
	size_t todo;
	size_t split;
	size_t pktlen;

	pktlen = rbuf->data[idx] << 8;
	pktlen |= rbuf->data[(idx + 1) % rbuf->size];
	if (offset > pktlen) return -EINVAL;
	if ((offset + len) > pktlen) len = pktlen - offset;

	idx = (idx + DVB_RINGBUFFER_PKTHDRSIZE + offset) % rbuf->size;
	todo = len;
	split = ((idx + len) > rbuf->size) ? rbuf->size - idx : 0;
	if (split > 0) {
		memcpy(buf, rbuf->data+idx, split);
		buf += split;
		todo -= split;
		idx = 0;
	}
	memcpy(buf, rbuf->data+idx, todo);
	return len;
}

void dvb_ringbuffer_pkt_dispose(struct dvb_ringbuffer *rbuf, size_t idx)
{
	size_t pktlen;

	rbuf->data[(idx + 2) % rbuf->size] = PKT_DISPOSED;

	// clean up disposed packets
	while(dvb_ringbuffer_avail(rbuf) > DVB_RINGBUFFER_PKTHDRSIZE) {
		if (DVB_RINGBUFFER_PEEK(rbuf, 2) == PKT_DISPOSED) {
			pktlen = DVB_RINGBUFFER_PEEK(rbuf, 0) << 8;
			pktlen |= DVB_RINGBUFFER_PEEK(rbuf, 1);
			DVB_RINGBUFFER_SKIP(rbuf, pktlen + DVB_RINGBUFFER_PKTHDRSIZE);
		} else {
			// first packet is not disposed, so we stop cleaning now
			break;
		}
	}
}

int vtunerc_ca_read_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	dprintk(priv->ctx, "CAM %i: ca_read_attribute_mem address=%i -> %i\n", slot, address, myaddress);
	if (myaddress<0) return 0xFF;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	return sl->tuple_mem[myaddress];
}

int vtunerc_ca_write_attribute_mem(struct dvb_ca_en50221 *ca, int slot, int address, u8 value)
{
	struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	int myaddress=address/2;
	dprintk(priv->ctx, "CAM %i: ca_write_attribute_mem address=%i -> %i value=%i\n", slot, address, myaddress, value);
	if (myaddress<0) return -EIO;
	if (myaddress>=sizeof(sl->tuple_mem)) return 0xFF;
	sl->tuple_mem[myaddress]=value;
	return 0;
}

int vtunerc_ca_read_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address)
{
	struct vtunerc_ca_private *priv = ca->data;
	struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	dprintk(priv->ctx, "CAM %i: ca_read_cam_control address=%i\n", slot, address);
	if (address==CTRLIF_STATUS) return sl->nextstatus;
	if (address==CTRLIF_SIZE_HIGH) return 0;
	if (address==CTRLIF_SIZE_LOW) return 2; // 2 bytes
	if (address==CTRLIF_DATA) return 0xff; // fake data
	return -EIO;
}

int vtunerc_ca_write_cam_control(struct dvb_ca_en50221 *ca, int slot, u8 address, u8 value)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	dprintk(priv->ctx, "CAM %i: ca_write_cam_control address=%i value=%i\n",slot, address, value);
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

int vtunerc_ca_send_ca_info(u8 *ebuf, u8 *rbuf, struct vtunerc_cainfo *info)
{
	int ptr, i, caids=0;
	u16 swapped;

	for (i=0; i<sizeof(info->caids)/2; i++) {
		if (info->caids[i]!=0) caids++;
	}

	// copy most of request
	memcpy(ebuf, rbuf, 12);
	ebuf[11]=0x31; // AOT_CA_INFO
	ebuf[12]=caids*2;
	ptr=13;
	for (i=0; i<sizeof(info->caids)/2; i++) {
		if (info->caids[i]!=0) {
			swapped = cpu_to_be16(info->caids[i]);
			memcpy(&ebuf[ptr], &swapped, 2);
			ptr+=2;
		}
	}
	return ptr;
}

int vtunerc_ca_send_pmt_reply(u8 *ebuf, u8 *rbuf, bool can_decrypt, struct vtunerc_cainfo *info)
{
	bool sidfound=false;
	bool sidsempty=true;
	int i;
	u16 sid;
	// copy most of request
	memcpy(ebuf, rbuf, 12);
	ebuf[11]=0x33; // AOT_CA_PMT_REPLY
	ebuf[12]=0x04; // length field
	sid = rbuf[14]*256+rbuf[15];
	ebuf[13]=rbuf[14]; // program_number
	ebuf[14]=rbuf[15];
	ebuf[15]=rbuf[16]; // version_number + current_next_indicator
	if (can_decrypt) {
		for (i=0; i<sizeof(info->sids)/2; i++) {
			if (info->sids[i]!=0) {
				sidsempty=false;
				if (info->sids[i]==sid) sidfound=true;
			}
		}
		if (sidsempty || sidfound)
			ebuf[16]=0x81; // "descrambling possible"
		else
			ebuf[16]=0x00;
	} else {
		ebuf[16]=0;
	}
	return 17;
}

int vtunerc_ca_send_app_info(u8 *ebuf, u8 *rbuf)
{
	// copy most of request
	memcpy(ebuf, rbuf, 12);
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
	struct vtunerc_pmt *pmt;
	u8 rbuffer[256], length_field, length_field_p;
	int ptr;
	size_t reqlen;
	ssize_t idx;

	if (!sl->rbuf.data) return -EIO;

	idx=dvb_ringbuffer_pkt_next(&sl->rbuf, -1, &reqlen);
	if (idx==-1) return 0;
	dvb_ringbuffer_pkt_read(&sl->rbuf, idx, 0, (u8*) &rbuffer, reqlen);
	dvb_ringbuffer_pkt_dispose(&sl->rbuf, idx);

	ecount=0;
	tpdu = (void *) rbuffer; // last request data received
	if (tpdu->tag==T_CREATE_TC) {
		dprintk(priv->ctx, "CAM %i: create tc\n", slot);
		ecount=vtunerc_ca_send_ctc_reply(ebuf, tpdu->slot, tpdu->tcid);
	} else if (tpdu->tag==T_DATA_LAST) {
		// now answer some requests
		if (reqlen<=6) {
			if (sl->ai_session && !sl->ca_session) {
				dprintk(priv->ctx,"CAM %i: create ca support session\n", slot);
			       	ecount=vtunerc_ca_send_session_request(ebuf, tpdu->slot, tpdu->tcid, RI_CONDITIONAL_ACCESS_SUPPORT);
				sl->ca_session=true;
			} else if (!sl->ai_session) {
				dprintk(priv->ctx,"CAM %i: create app info session\n", slot);
				ecount=vtunerc_ca_send_session_request(ebuf, tpdu->slot, tpdu->tcid, RI_APPLICATION_INFORMATION);
				sl->ai_session=true;
			} else {
				ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
			}
		} else {
			length_field = rbuffer[3];
			if ((length_field & 0x80) == 0x80) {
				length_field &= 0x7f;
				spdu = (void *) &rbuffer[4+length_field];
			} else {
				length_field = 0;
				spdu = (void *) &rbuffer[4];
			}
			if (spdu->tag == ST_SESSION_NUMBER) {
				int aot = spdu->aot0<<16 | spdu->aot1<<8 | spdu->aot2;
				switch (aot) {
					case AOT_CA_INFO_ENQ:
						dprintk(priv->ctx,"CAM %i: answer ca_info_enq\n", slot);
						ecount=vtunerc_ca_send_ca_info(ebuf, (u8*) &rbuffer, &sl->info);
						if (ecount<=13) sl->can_decrypt=false; else sl->can_decrypt=true;
						break;
					case AOT_CA_PMT:
						length_field_p = rbuffer[12+length_field];
						if ((length_field_p & 0x80) == 0x80) {
							length_field_p &= 0x7f;
							pmt = (void *) &rbuffer[13+length_field+length_field_p];
						} else {
							length_field_p = 0;
							pmt = (void *) &rbuffer[13+length_field];
						}
						//dprintk(priv->ctx,"CAM %i: list_management=%i program-number=%i pil=%i cmd=%i\n", slot, pmt->list_management, cpu_to_be16(pmt->program_number), cpu_to_be16(pmt->program_info_length), pmt->ca_pmt_cmd_id);
						if (pmt->list_management>=3 && pmt->list_management<=5) {
							if (pmt->program_info_length!=0) {
								if (rbuffer[19+length_field+length_field_p]==0x04) {
									printk(KERN_INFO "vtunerc%d: CAM %i: unassigned service id %x (%i)\n", priv->ctx->idx, slot,
											sl->info.service, sl->info.service);
									sl->info.service_last = sl->info.service;
									sl->info.pmt_last = sl->info.pmt;
									sl->info.pid = 0;
									sl->info.pmt = 0;
									sl->info.service = 0;
									ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
								}
								if (rbuffer[19+length_field+length_field_p]==0x01) {
									ptr = 20+length_field+length_field_p+cpu_to_be16(pmt->program_info_length);
									if (ptr+1<reqlen) {
										sl->info.pid = rbuffer[ptr]*256+rbuffer[ptr+1];
										sl->info.service = cpu_to_be16(pmt->program_number);
										if (sl->info.service == sl->info.service_last)
											sl->info.pmt = sl->info.pmt_last;
										else
											sl->info.pmt = 0;
										printk(KERN_INFO "vtunerc%d: CAM %i: assigned service id %x (%i) - pid %x (%i)\n",
												priv->ctx->idx, slot, sl->info.service, sl->info.service,
												sl->info.pid, sl->info.pid);
									}
									ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
								}
								if (rbuffer[19+length_field+length_field_p]==0x03) {
									dprintk(priv->ctx, "CAM %i: answer ca_pmt %s\n", slot, sl->can_decrypt ? "true" : "false");
									ecount=vtunerc_ca_send_pmt_reply(ebuf, (u8 *) &rbuffer, sl->can_decrypt, &sl->info);
								}
							} else {
								if (pmt->list_management==0x04 && pmt->program_number) {
									ptr = 20+length_field+length_field_p;
									if (ptr+1<reqlen) {
                                                                                sl->info.pid = rbuffer[ptr]*256+rbuffer[ptr+1];
                                                                                sl->info.service = cpu_to_be16(pmt->program_number);
										if (sl->info.service == sl->info.service_last)
											sl->info.pmt = sl->info.pmt_last;
										else
											sl->info.pmt = 0;
                                                                                printk(KERN_INFO "vtunerc%d: CAM %i: assigned service id %x (%i) - pid %x (%i)\n",
                                                                                                priv->ctx->idx, slot, sl->info.service, sl->info.service,
												sl->info.pid, sl->info.pid);
                                                                        }
                                                                        ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
								}
							}
						} else {
							ecount=vtunerc_ca_send_sb_reply(ebuf, tpdu->slot, tpdu->tcid);
						}
						break;
					case AOT_APPLICATION_INFO_ENQ:
						dprintk(priv->ctx,"CAM %i: answer app_info_enq\n", slot);
						ecount=vtunerc_ca_send_app_info(ebuf, (u8*) &rbuffer);
						break;
				}
		     }
	     }
	}
	if (ecount<=255 && ebuf[2]!=T_SB) print_hex_dump_bytes("ca_read_data  ", DUMP_PREFIX_NONE, ebuf, ecount);
	return ecount;
}

static int vtunerc_ca_write_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	if (ecount>(BUFFER_SIZE/8)) return -EINVAL;
	if (!sl->rbuf.data) return -EIO;

	dvb_ringbuffer_pkt_write(&sl->rbuf, ebuf, ecount);
	if (ebuf[2]==T_DATA_LAST && ebuf[3]==1) return ecount;
	print_hex_dump_bytes("ca_write_data ", DUMP_PREFIX_NONE, ebuf, ecount);
	return ecount;
}

int vtunerc_ca_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	dvb_ca_en50221_camready_irq(ca, slot);
	return 0;
}

int vtunerc_ca_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	int idx;
	size_t pktlen;

        struct vtunerc_ca_private *priv = ca->data;
        struct vtunerc_ca_slot *sl = &priv->slot_info[slot];

	sl->info.pid = 0;
	sl->info.pmt = 0;
	sl->info.pmt_last = 0;
	sl->info.service = 0;
	sl->info.service_last = 0;
	sl->can_decrypt = false;
	sl->ca_session = false;
	sl->ai_session = false;
	if (sl->rbuf.data) {
		do {
			idx=dvb_ringbuffer_pkt_next(&sl->rbuf, -1, &pktlen);
			if (idx!=-1) dvb_ringbuffer_pkt_dispose(&sl->rbuf, idx);
		} while (idx!=-1);
	}
	sl->nextstatus=STATUSREG_DA|STATUSREG_FR;
	return 0;
}

int vtunerc_ca_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	return 0;
}

int vtunerc_ca_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct vtunerc_ca_private *priv = ca->data;
	dprintk(priv->ctx, "CAM %i: get slot status (open=%i)\n", slot, open);
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

struct vtunerc_cainfo *vtunerc_ca_find(struct vtunerc_ctx *ctx, int pid, int service)
{
	int i;
	struct vtunerc_ca_private *priv;
	if (!ctx) return NULL;
	if (!ctx->pubca.data) return NULL;

	priv = ctx->pubca.data;
	for (i=0; i< priv->slot_count; i++) {
		struct vtunerc_ca_slot *sl = &priv->slot_info[i];
		if (pid!=0 && service!=0) {
			if (sl->info.pid==pid && sl->info.service==service) return &sl->info;
		} else if (pid!=0) {
			if (sl->info.pid==pid) return &sl->info;
		} else if (service!=0) {
			if (sl->info.service==service) return &sl->info;
		}
	}
	return NULL;
}

struct vtunerc_cainfo *vtunerc_ca_get(struct vtunerc_ctx *ctx, int slot)
{
	int i;
        struct vtunerc_ca_private *priv;
        if (!ctx) return NULL;
        if (!ctx->pubca.data) return NULL;

        priv = ctx->pubca.data;
        for (i=0; i< priv->slot_count; i++) {
		struct vtunerc_ca_slot *sl = &priv->slot_info[i];
		if (sl->info.slot==slot) return &sl->info;
	}
	return NULL;
}

int vtunerc_ca_init(struct vtunerc_ctx *ctx)
{
	const int slot_count = VTUNER_MAX_SLOTS;
	int ret;
	int i;
	struct vtunerc_ca_private *ca = NULL;
	struct vtunerc_ca_slot *sl;
	void *buf;

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
		sl = &ca->slot_info[i];
		sl->nextstatus=0xC0;
		sl->info.slot=i;
		memcpy(sl->tuple_mem,&tuple_mem,sizeof(tuple_mem));
		buf = vmalloc(BUFFER_SIZE);
		if (!buf) printk(KERN_ERR "vtunerc%d: failed to allocated ringbuffer, cam slot %i disabled\n", ctx->idx, i);
		if (buf) dvb_ringbuffer_init(&sl->rbuf, buf, BUFFER_SIZE);
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
	ctx->pubca.poll_slot_status = vtunerc_ca_slot_status;
	ret = dvb_ca_en50221_init(&ctx->dvb_adapter, &ctx->pubca,
			DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE |
			DVB_CA_EN50221_FLAG_IRQ_FR |
			DVB_CA_EN50221_FLAG_IRQ_DA, slot_count);

	for (i=0; i < slot_count; i++) {
		dvb_ca_en50221_frda_irq(&ctx->pubca, i); // enable "irq"
	}
	return ret;
}

int vtunerc_ca_clear(struct vtunerc_ctx *ctx)
{
	int i;

	if (!ctx) return -EINVAL;
	if (ctx->pubca.private!=NULL) dvb_ca_en50221_release(&ctx->pubca);
	ctx->pubca.private=NULL;
	if (ctx->pubca.data) {
		struct vtunerc_ca_private *ca = ctx->pubca.data;
		for (i=0; i<ca->slot_count; i++) {
			struct vtunerc_ca_slot *sl = &ca->slot_info[i];
			if (sl->rbuf.data) vfree(sl->rbuf.data);
		}
		kfree(ca->slot_info);
		kfree(ca);
		ctx->pubca.data=NULL;
	}
	return 0;
}
