/*
 * satip: tuning and pid config
 *
 * Copyright (C) 2014  mc.fishdish@gmail.com
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

#ifndef _SATIP_CONFIG_H
#define _SATIP_CONFIG_H

#include "vtuner.h"

#define UNUSED(x) (void)(x)

typedef enum
  {
    SATIPCFG_UNSET = -1,
    SATIPCFG_P_HORIZONTAL = 0,
    SATIPCFG_P_VERTICAL,
    SATIPCFG_P_CIRC_LEFT,
    SATIPCFG_P_CIRC_RIGHT
  } t_polarization;

typedef enum
  {
    SATIPCFG_R_0_35 = 0,
    SATIPCFG_R_0_20,
    SATIPCFG_R_0_25,
    SATIPCFG_R_AUTO,
    SATIPCFG_R_0_15,
    SATIPCFG_R_0_10,
    SATIPCFG_R_0_05
  } t_roll_off;

typedef enum
  {
    SATIPCFG_P_ON = 0,
    SATIPCFG_P_OFF,
    SATIPCFG_P_AUTO
  } t_pilots;

typedef enum
  {
    SATIPCFG_NONE = 0,
    SATIPCFG_F_12,
    SATIPCFG_F_23,
    SATIPCFG_F_34,
    SATIPCFG_F_45,
    SATIPCFG_F_56,
    SATIPCFG_F_67,
    SATIPCFG_F_78,
    SATIPCFG_F_89,
    SATIPCFG_F_AUTO,
    SATIPCFG_F_35,
    SATIPCFG_F_910,
    SATIPCFG_F_25
  } t_fec_inner;


typedef enum
  {
    SATIPCFG_INCOMPLETE = 0, /* parameters are missing */
    SATIPCFG_PID_CHANGED,    /* only PIDs were touched, allows for "PLAY" with addpid/delpids */
    SATIPCFG_CHANGED,        /* requires new tuning */
    SATIPCFG_SETTLED,         /* configuration did not change since last access */
    SATIPCFG_CLOSING,
  } t_satip_config_status;



#define SATIPCFG_MAX_PIDS MAX_PIDTAB_LEN

typedef struct satip_config
{
  /* status info */
  t_satip_config_status status;

  /* current/new configuration */
  unsigned int      delsys;
  unsigned int      frequency;
  t_polarization    polarization;
  t_roll_off        roll_off;
  unsigned int      mod_type;
  t_pilots          pilots;
  unsigned int      symbol_rate;
  t_fec_inner       fec_inner;   
  unsigned int      inversion;

  /* remote frontend */
  int               frontend;

  /* pids */
  unsigned short    pid[SATIPCFG_MAX_PIDS];  
  
  /* delta info for addpids/delpids cmd */
  unsigned short    mod_pid[SATIPCFG_MAX_PIDS];

  /* sat number as position  */
  int position;
} t_satip_config;




#define SATIPCFG_NOCHANGE -1
#define SATIPCFG_OK       0
#define SATIPCFG_ERROR    1


t_satip_config* satip_new_config(int frontend);

int satip_del_pid(t_satip_config* cfg,unsigned short pid);
int satip_add_pid(t_satip_config* cfg,unsigned short pid);
void satip_add_default_pids(t_satip_config *cfg);
void satip_del_allpid(t_satip_config* cfg);

int satip_set_position(t_satip_config* cfg, int position);

int satip_set_dvbs(t_satip_config* cfg, unsigned int freq, t_polarization pol, unsigned int modtype, unsigned int symrate, t_fec_inner fecinner);
int satip_set_dvbs2(t_satip_config* cfg, unsigned int freq, t_polarization pol, unsigned int modtype, unsigned int symrate, t_fec_inner fecinner, t_roll_off rolloff, t_pilots pilots);
int satip_set_dvbc(t_satip_config* cfg, unsigned int freq, unsigned int inversion, unsigned int modtype, unsigned int symrate);

int satip_valid_config(t_satip_config* cfg);
int satip_tuning_required(t_satip_config* cfg);
int satip_pid_update_required(t_satip_config* cfg);

int satip_prepare_tuning(t_satip_config* cfg, char* str, int maxlen);
int satip_prepare_pids(t_satip_config* cfg, char* str, int maxlen,int modpid);

int satip_settle_config(t_satip_config* cfg);
void satip_clear_config(t_satip_config* cfg);

void satip_close(t_satip_config* cfg);
int satip_close_requested(t_satip_config* cfg);
#endif

