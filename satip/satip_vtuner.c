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


static void set_voltage(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  if ( msg->body.fe_params.u.qpsk.sat.voltage == SEC_VOLTAGE_13 )
    satip_set_polarization(vt->satip_cfg, SATIPCFG_P_VERTICAL);
  else if (msg->body.fe_params.u.qpsk.sat.voltage == SEC_VOLTAGE_18)
    satip_set_polarization(vt->satip_cfg, SATIPCFG_P_HORIZONTAL);
  else  /*  SEC_VOLTAGE_OFF */
    satip_lnb_off(vt->satip_cfg);

  DEBUG(MSG_MAIN,"set voltage:  %d\n",msg->body.fe_params.u.qpsk.sat.voltage);
}

static void diseqc_msg(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  char dbg[50];
  struct diseqc_master_cmd* cmd=&msg->body.fe_params.u.qpsk.sat.diseqc_master_cmd;

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
	satip_set_polarization(vt->satip_cfg, SATIPCFG_P_HORIZONTAL);	
      else if ( (data & 0x22) == 0x20 )
	satip_set_polarization(vt->satip_cfg, SATIPCFG_P_VERTICAL);

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
}

static void set_frontend(struct satip_vtuner* vt, struct vtuner_message* msg)
{
  set_voltage(vt, msg);
  diseqc_msg(vt, msg);

  if (msg->body.fe_params.u.qpsk.sat.burst == SEC_MINI_A)
      satip_set_position(vt->satip_cfg,1);
  if (msg->body.fe_params.u.qpsk.sat.burst == SEC_MINI_B)
      satip_set_position(vt->satip_cfg,2);

  int frequency = msg->body.fe_params.frequency/100;

  /* revert frequency shift */
  if ( msg->body.fe_params.u.qpsk.sat.tone == SEC_TONE_ON ) /* high band */
    frequency += 106000;
  else /* low band */
    if ( frequency-97500 < 0 )
      frequency+=97500;
    else
      frequency-=97500;

  satip_set_freq(vt->satip_cfg, frequency);

  /* symbol rate */
  satip_set_symbol_rate(vt->satip_cfg, msg->body.fe_params.u.qpsk.symbol_rate/1000 );

  /* FEC */
  satip_set_fecinner(vt->satip_cfg, msg->body.fe_params.u.qpsk.fec_inner);

  /* delivery system */
  satip_set_modsys(vt->satip_cfg, msg->body.fe_params.delivery_system);

  /* Modulation */
  satip_set_modtype(vt->satip_cfg, msg->body.fe_params.u.qpsk.modulation);

  /* RollOff */
  satip_set_rolloff(vt->satip_cfg, msg->body.fe_params.u.qpsk.rolloff);

  /* Pilot */
  satip_set_pilots(vt->satip_cfg, msg->body.fe_params.u.qpsk.pilot);

  DEBUG(MSG_MAIN,"MSG_SET_FRONTEND freq: %d symrate: %d fec: %s rol: %s \n",
	frequency,
	msg->body.fe_params.u.qpsk.symbol_rate,
	strmap_fecinner[msg->body.fe_params.u.qpsk.fec_inner],
	strmap_rolloff[msg->body.fe_params.u.qpsk.rolloff] );
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
	if (!hdr) {
	  DEBUG(MSG_MAIN,"MSG_SET_PIDLIST:\n");
	  hdr=1;
	}
        satip_add_pid(vt->satip_cfg,pidlist[i]);
	DEBUG(MSG_MAIN,"%d\n",pidlist[i]);
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
