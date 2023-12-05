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


typedef enum
  {
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
    SATIPCFG_MS_DVB_S = 5,
    SATIPCFG_MS_DVB_S2
  } t_mod_sys;

typedef enum
  {
    SATIPCFG_MT_QPSK = 0,
    SATIPCFG_MT_8PSK = 9
  } t_mod_type;

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
    SATIPCFG_SETTLED         /* configuration did not change since last access */
  } t_satip_config_status;



#define SATIPCFG_MAX_PIDS 64

typedef struct satip_config
{
  /* status info */
  t_satip_config_status status;
  unsigned int          param_cfg;

  /* current/new configuration */
  unsigned int      frequency;
  t_polarization    polarization;
  int               lnb_off;
  t_roll_off        roll_off;
  t_mod_sys         mod_sys;
  t_mod_type        mod_type;
  t_pilots          pilots;
  unsigned int      symbol_rate;
  t_fec_inner       fec_inner;   
  int               position;
  int               frontend;
  unsigned short    pid[SATIPCFG_MAX_PIDS];  
  
  /* delta info for addpids/delpids cmd */
  unsigned short    mod_pid[SATIPCFG_MAX_PIDS];

} t_satip_config;





#define SATIPCFG_OK       0
#define SATIPCFG_ERROR    1


t_satip_config* satip_new_config(int frontend);

int satip_del_pid(t_satip_config* cfg,unsigned short pid);
int satip_add_pid(t_satip_config* cfg,unsigned short pid);
void satip_del_allpid(t_satip_config* cfg);

int satip_set_freq(t_satip_config* cfg,unsigned int freq);
int satip_set_polarization(t_satip_config* cfg,t_polarization pol);
int satip_lnb_off(t_satip_config* cfg);
int satip_set_rolloff(t_satip_config* cfg,t_roll_off rolloff);
int satip_set_modsys(t_satip_config* cfg,t_mod_sys modsys);
int satip_set_modtype(t_satip_config* cfg,t_mod_type modtype);
int satip_set_pilots(t_satip_config* cfg,t_pilots pilots);
int satip_set_symbol_rate(t_satip_config* cfg,unsigned int symrate);
int satip_set_fecinner(t_satip_config* cfg, t_fec_inner fecinner);
int satip_set_position(t_satip_config* cfg, int position);


int satip_valid_config(t_satip_config* cfg);
int satip_tuning_required(t_satip_config* cfg);
int satip_pid_update_required(t_satip_config* cfg);

int satip_prepare_tuning(t_satip_config* cfg, char* str, int maxlen);
int satip_prepare_pids(t_satip_config* cfg, char* str, int maxlen,int modpid);

int satip_settle_config(t_satip_config* cfg);
void satip_clear_config(t_satip_config* cfg);

#endif

