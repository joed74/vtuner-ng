/*
 * satip: vtuner to satip mapping
 *
 * Copyright (C) 2014  mc.fishdish@gmail.com
 * [fragments from vtuner by Honza Petrous]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include "satip_config.h"
#include "satip_vtuner.h"
#include "log.h"

/* from vtunerc_priv.h */
#define MAX_PIDTAB_LEN 30
#define PID_UNKNOWN 0x0FFFF

/* fixme: align with driver */
typedef unsigned int   u32;
typedef signed int     s32;
typedef unsigned short u16;
typedef unsigned char  u8;

/* driver interface */
#include "vtuner.h"



extern char* const strmap_fecinner[];
extern char* const strmap_rolloff[];

typedef struct satip_vtuner {
  int fd;
  t_satip_config* satip_cfg;
} t_satip_vtuner;


t_satip_vtuner* satip_vtuner_new(char* devname,t_satip_config* satip_cfg)
{
  int fd;
  t_satip_vtuner* vt;

  fd  = open(devname, O_RDWR);
  if ( fd < 0)
    {
      ERROR(MSG_MAIN,"Couldn't open %s\n",devname);
      return NULL;
    }

  vt=(t_satip_vtuner*)malloc(sizeof(t_satip_vtuner));

  vt->fd=fd;
  vt->satip_cfg=satip_cfg;

  /* set default position A, if appl. does not configure */
  satip_set_position(satip_cfg,1);

  return vt;
}

int satip_vtuner_fd(struct satip_vtuner* vt)
{
  return vt->fd;
}

static t_polarization get_polarization(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  char dbg[50];
  struct diseqc_master_cmd* cmd=&msg->body.fe_params.u.qpsk.sat.diseqc_master_cmd;
  t_polarization ret = SATIPCFG_UNSET;

  if ( cmd->msg[0] == 0xe0 &&
       cmd->msg[1] == 0x10 &&
       cmd->msg[2] == 0x38 &&
       cmd->msg_len == 4 )
    {
      /* committed switch */
      u8 data=cmd->msg[3];

      if ( (data & 0x01) == 0x01 )
	{
	  msg->body.fe_params.u.qpsk.sat.tone = SEC_TONE_ON; /* high band */
	}
      else if ( (data & 0x11) == 0x10 )
	{
	  msg->body.fe_params.u.qpsk.sat.tone = SEC_TONE_OFF; /* low band */
	}

      if ( (data & 0x02) == 0x02 )
	ret = SATIPCFG_P_HORIZONTAL;
      else if ( (data & 0x22) == 0x20 )
	ret = SATIPCFG_P_VERTICAL;

      /* some invalid combinations ? */
      satip_set_position(vt->satip_cfg, ( (data & 0x0c) >> 2) + 1 );


      sprintf(dbg,"%02x %02x %02x   msg %02x %02x %02x len %d",
	  cmd->msg[0],
	  cmd->msg[1],
	  cmd->msg[2],
	  cmd->msg[3],
	  cmd->msg[4],
	  cmd->msg[5],
	  cmd->msg_len);
       DEBUG(MSG_MAIN,"MSG_SEND_DISEQC_MSG:  %s\n",dbg);
    }
  else
    {
      if (msg->body.fe_params.u.qpsk.sat.voltage == SEC_VOLTAGE_13)
	ret = SATIPCFG_P_VERTICAL;
      else if (msg->body.fe_params.u.qpsk.sat.voltage == SEC_VOLTAGE_18)
	ret = SATIPCFG_P_HORIZONTAL;
    }
  if (msg->body.fe_params.u.qpsk.sat.burst == SEC_MINI_A)
     satip_set_position(vt->satip_cfg,1);
  if (msg->body.fe_params.u.qpsk.sat.burst == SEC_MINI_B)
     satip_set_position(vt->satip_cfg,2);
  return ret;
}

static void set_frontend(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  t_polarization pol;
  switch (msg->body.fe_params.delivery_system)
  {
     case SATIPCFG_MS_DVB_S:
       pol = get_polarization(vt, msg);
       satip_set_dvbs(vt->satip_cfg, msg->body.fe_params.frequency,
		       msg->body.fe_params.u.qpsk.sat.tone, pol,
		       msg->body.fe_params.u.qpsk.modulation,
		       msg->body.fe_params.u.qpsk.symbol_rate,
		       msg->body.fe_params.u.qpsk.fec_inner);
       break;
     case SATIPCFG_MS_DVB_S2:
       pol = get_polarization(vt, msg);
       satip_set_dvbs2(vt->satip_cfg, msg->body.fe_params.frequency,
		       msg->body.fe_params.u.qpsk.sat.tone, pol,
		       msg->body.fe_params.u.qpsk.modulation,
		       msg->body.fe_params.u.qpsk.symbol_rate,
		       msg->body.fe_params.u.qpsk.fec_inner,
		       msg->body.fe_params.u.qpsk.rolloff,
		       msg->body.fe_params.u.qpsk.pilot);
       break;

      default:
       ERROR(MSG_MAIN,"unsupported delsys %i\n", msg->body.fe_params.delivery_system);
       break;
  }
}

static void set_pidlist(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  int i;
  u16* pidlist=msg->body.pidlist;

  satip_del_allpid(vt->satip_cfg);

  int hdr=0;
  for (i=0; i<MAX_PIDTAB_LEN; i++)
    if (pidlist[i] < 8192  )
      {
        if (satip_add_pid(vt->satip_cfg,pidlist[i])==SATIPCFG_OK) {
	  if (!hdr) {
	    DEBUG(MSG_MAIN,"MSG_SET_PIDLIST:\n");
	    hdr=1;
	  }
	  DEBUG(MSG_MAIN,"%d\n",pidlist[i]);
	}
      }
}

void satip_vtuner_event(struct satip_vtuner* vt)
{
  struct vtuner_message  msg;

  if (ioctl(vt->fd, VTUNER_GET_MESSAGE, &msg))
    return;

  switch(msg.type)
    {
    case MSG_SET_FRONTEND:
      set_frontend(vt,&msg);
      break;

    case MSG_PIDLIST:
      set_pidlist(vt,&msg);
      return;
      break;

    default:
      break;
    }

  msg.type=0;

  if (ioctl(vt->fd, VTUNER_SET_RESPONSE, &msg)){
    ERROR(MSG_MAIN,"ioctl: response not ok\n");
    return;
  }
}
