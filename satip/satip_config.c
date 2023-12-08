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

#include <stdlib.h>
#include <stdio.h>
#include "satip_config.h"
#include "log.h"

/* configuration items present */
#define  PC_FREQ      0x0001
#define  PC_POL       0x0002
#define  PC_ROLLOFF   0x0004
#define  PC_MODSYS    0x0008
#define  PC_MODTYPE   0x0010
#define  PC_PILOTS    0x0020
#define  PC_SYMRATE   0x0040
#define  PC_FECINNER  0x0080
#define  PC_POSITION  0x0100

#define  PC_COMPLETE_DVBS   \
  ( PC_FREQ | PC_POL | PC_MODSYS | PC_SYMRATE | \
    PC_FECINNER | PC_POSITION )

#define  PC_COMPLETE_DVBS2  \
  ( PC_FREQ | PC_POL | PC_ROLLOFF | PC_MODSYS | \
    PC_MODTYPE | PC_PILOTS | PC_SYMRATE | \
    PC_FECINNER | PC_POSITION )


/* PID handling */
#define PID_VALID  0
#define PID_IGNORE 1
#define PID_ADD    2
#define PID_DELETE 3

/* strings for query strings */
char  const strmap_polarization[] = { 'h', 'v', 'l', 'r' };
char* const strmap_fecinner[] = { "","12","23","34","45","56","67","78","89","AUTO","35","910","25" };
char* const strmap_rolloff[] = { "0.35","0.20","0.25","AUTO","0.15","0.10","0.05" };




t_satip_config* satip_new_config(int frontend)
{
  t_satip_config* cfg;

  cfg=(t_satip_config*) malloc(sizeof(t_satip_config));

  cfg->frontend = frontend;

  satip_clear_config(cfg);

  return cfg;
}



/*
 * PIDs need extra handling to cover "addpids" and "delpids" use cases
 */


static void pidupdate_status(t_satip_config* cfg)
{
  int i;
  int mod_found=0;

  for (i=0; i<SATIPCFG_MAX_PIDS; i++)
    if ( cfg->mod_pid[i] == PID_ADD ||
	 cfg->mod_pid[i] == PID_DELETE )
      {
	mod_found=1;
	break;
      }

  switch (cfg->status)
    {
    case SATIPCFG_SETTLED:
      if (mod_found)
	cfg->status = SATIPCFG_PID_CHANGED;
      break;
      
    case SATIPCFG_PID_CHANGED:
      if (!mod_found)
	cfg->status = SATIPCFG_SETTLED;
      break;

    case SATIPCFG_CHANGED:
    case SATIPCFG_INCOMPLETE:
      break;
    }
}

void satip_del_allpid(t_satip_config* cfg)
{
  int i;

  for ( i=0; i<SATIPCFG_MAX_PIDS; i++ )
    satip_del_pid(cfg, cfg->pid[i]);
}
      

int satip_del_pid(t_satip_config* cfg,unsigned short pid)
{
  int i;

  for (i=0; i<SATIPCFG_MAX_PIDS; i++)
    {
      if ( cfg->pid[i] == pid )
	switch (cfg->mod_pid[i]) 
	  {	    
	  case PID_VALID: /* mark it for deletion */
	    cfg->mod_pid[i] = PID_DELETE;
	    pidupdate_status(cfg);
	    return SATIPCFG_OK;

	  case PID_ADD:   /* pid shall be added, ignore it */
	    cfg->mod_pid[i] = PID_IGNORE;
	    pidupdate_status(cfg);
	    return SATIPCFG_OK;

	  case PID_IGNORE:
	    break;
	    
	  case PID_DELETE: /* pid shall already be deleted*/
	    return SATIPCFG_OK;
	  }
    }
  
  /* pid was not found, ignore request */
  return SATIPCFG_OK;
}



int satip_add_pid(t_satip_config* cfg,unsigned short pid)
{
  int i;

  /* check if pid is present and valid, to be added, to be deleted */
  for (i=0; i<SATIPCFG_MAX_PIDS; i++)
    {
      if ( cfg->pid[i] == pid )
	switch (cfg->mod_pid[i]) 
	  {	    
	  case PID_VALID: /* already present */
	  case PID_ADD:   /* pid shall be already added */
	    /* just return current status, no update required */
	    return SATIPCFG_OK;

	  case PID_IGNORE:
	    break;
	    
	  case PID_DELETE: 
	    /* pid shall be deleted, make it valid again */
	    cfg->mod_pid[i] = PID_VALID;
	    pidupdate_status(cfg);
	    return SATIPCFG_OK;	    
	  }
    }
  
  /* pid was not found, add it */
  for ( i=0; i<SATIPCFG_MAX_PIDS; i++)
    {
      if (cfg->mod_pid[i] == PID_IGNORE )
	{
	  cfg->mod_pid[i] = PID_ADD;
	  cfg->pid[i] = pid;
	  pidupdate_status(cfg);
	  return SATIPCFG_OK;
	}
    }
  
  /* could not add it */
  return SATIPCFG_ERROR;
}






static void  param_update_status(t_satip_config* cfg)
{
  if ( cfg->param_cfg & PC_MODSYS )
    {
      if ( (cfg->mod_sys == SATIPCFG_MS_DVB_S && (cfg->param_cfg & PC_COMPLETE_DVBS) == PC_COMPLETE_DVBS ) ||
	   (cfg->param_cfg & PC_COMPLETE_DVBS2) == PC_COMPLETE_DVBS2 )
	cfg->status = SATIPCFG_CHANGED;
    }
  else /* modulation system is not set */
    {

      cfg->status = SATIPCFG_INCOMPLETE;
    }

  DEBUG(MSG_MAIN,"Status %04x \n",cfg->param_cfg);
}

int satip_set_freq(t_satip_config* cfg,unsigned int freq)
{
  if ( (cfg->param_cfg & PC_FREQ) &&  cfg->frequency == freq )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_FREQ;
  cfg->frequency = freq;

  param_update_status(cfg);
  return SATIPCFG_OK;
}

int satip_set_polarization(t_satip_config* cfg,t_polarization pol)
{
  if ( (cfg->param_cfg & PC_POL) &&  cfg->polarization == pol )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_POL;
  cfg->polarization = pol;

  /*  polarization shall only trigger tuning if it was powered down explicitly */
  if ( cfg->lnb_off==1 )
    {
      cfg->lnb_off = 0;
      param_update_status(cfg); 
    }

  return SATIPCFG_OK;
}


int satip_lnb_off(t_satip_config* cfg)
{
  cfg->lnb_off = 1;
  cfg->param_cfg &= ~PC_POL;
  
  cfg->status = SATIPCFG_INCOMPLETE;

  return SATIPCFG_OK;
}


int satip_set_rolloff(t_satip_config* cfg,t_roll_off rolloff)
{
  if ( (cfg->param_cfg & PC_ROLLOFF) &&  cfg->roll_off == rolloff )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_ROLLOFF;
  cfg->roll_off = rolloff;

  param_update_status(cfg);
  return SATIPCFG_OK;
}



int satip_set_modsys(t_satip_config* cfg,t_mod_sys modsys)
{
  if ( (cfg->param_cfg & PC_MODSYS) &&  cfg->mod_sys == modsys )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_MODSYS;
  cfg->mod_sys = modsys;

  param_update_status(cfg);
  return SATIPCFG_OK;
}

int satip_set_modtype(t_satip_config* cfg,t_mod_type modtype)
{
  if ( (cfg->param_cfg & PC_MODTYPE) &&  cfg->mod_type == modtype )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_MODTYPE;
  cfg->mod_type = modtype;

  param_update_status(cfg);
  return SATIPCFG_OK;
}


int satip_set_pilots(t_satip_config* cfg,t_pilots pilots)
{
  if ( (cfg->param_cfg & PC_PILOTS) &&  cfg->pilots == pilots )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_PILOTS;
  cfg->pilots = pilots;

  param_update_status(cfg);
  return SATIPCFG_OK;
}

int satip_set_symbol_rate(t_satip_config* cfg,unsigned int symrate)
{
  if ( (cfg->param_cfg & PC_SYMRATE) &&  cfg->symbol_rate == symrate )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_SYMRATE;
  cfg->symbol_rate = symrate;

  param_update_status(cfg);
  return SATIPCFG_OK;
}


int satip_set_fecinner(t_satip_config* cfg, t_fec_inner fecinner)
{
  if ( (cfg->param_cfg & PC_FECINNER) &&  cfg->fec_inner == fecinner )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_FECINNER;
  cfg->fec_inner = fecinner;

  param_update_status(cfg);
  return SATIPCFG_OK;
}

int satip_set_position(t_satip_config* cfg, int position)
{
  if ( (cfg->param_cfg & PC_POSITION) &&  cfg->position == position )
    return SATIPCFG_OK;

  cfg->param_cfg |= PC_POSITION;
  cfg->position = position;

  param_update_status(cfg);
  return SATIPCFG_OK;
}


int satip_valid_config(t_satip_config* cfg)
{
  return ( cfg->status != SATIPCFG_INCOMPLETE );
}


int satip_tuning_required(t_satip_config* cfg)
{
  return ( cfg->status == SATIPCFG_CHANGED );
}

int satip_pid_update_required(t_satip_config* cfg)
{
  return ( cfg->status == SATIPCFG_PID_CHANGED );
}


static int setpidlist(t_satip_config* cfg, char* str,int maxlen,const char* firststr,int modtype1, int modtype2)
{
  int i;
  int printed=0;
  int first=1;
  
  for ( i=0; i<SATIPCFG_MAX_PIDS; i++ )
	if ( cfg->mod_pid[i] == modtype1 ||
	     cfg->mod_pid[i] == modtype2 )
	  {
	    printed += snprintf(str+printed, maxlen-printed, "%s%d",
			       first ? firststr : ",",
			       cfg->pid[i]);
	    first=0;

	    if ( printed>=maxlen )
	      return printed;
	  }

  return printed;
}


int satip_prepare_tuning(t_satip_config* cfg, char* str, int maxlen)
{
  int printed;
  char frontend_str[7]="";
  
  /* optional: specific frontend */
  if ( cfg->frontend > 0 && cfg->frontend<100)
    sprintf(frontend_str, "fe=%d&", cfg->frontend);

  /* DVB-S mandatory parameters */
  printed = snprintf(str, maxlen, 
		     "src=%d&%sfreq=%d.%d&pol=%c&msys=%s&mtype=%s&sr=%d&fec=%s",
		     cfg->position,
		     frontend_str,
		     cfg->frequency/10, cfg->frequency%10,
		     strmap_polarization[cfg->polarization],
		     cfg->mod_sys == SATIPCFG_MS_DVB_S ? "dvbs" : "dvbs2",
                     cfg->mod_type == SATIPCFG_MT_QPSK ? "qpsk" : "8psk",
		     cfg->symbol_rate,
		     strmap_fecinner[cfg->fec_inner]);

  if ( printed>=maxlen )
    return printed;
  str += printed;

  /* DVB-S2 additional required parameters */
  if ( cfg->mod_sys == SATIPCFG_MS_DVB_S2 )
    {
      printed += snprintf(str, maxlen-printed, "&ro=%s&plts=%s",
			 strmap_rolloff[cfg->roll_off],
			 cfg->pilots == SATIPCFG_P_OFF ? "off" : "on" );
    }

  /* don´t forget to check on caller ! */
  return printed;  
}


int satip_prepare_pids(t_satip_config* cfg, char* str, int maxlen,int modpid)
{
  int printed;

  if (modpid)
    {
      printed = setpidlist(cfg,str,maxlen,"addpids=",PID_ADD, PID_ADD);

      if ( printed>=maxlen )
	return printed;

      printed += setpidlist(cfg, str+printed,maxlen-printed,
			    printed>0 ? "&delpids=" : "delpids=",PID_DELETE, PID_DELETE);
    }
  else
    {
      printed = setpidlist(cfg,str,maxlen,"pids=",PID_VALID, PID_ADD);     
    }

  /* nothing was added, use "none" */
  if ( printed == 0 )
    {
      printed = snprintf(str,maxlen,"pids=none");
    }
  
  /* don´t forget to check on caller */
  return printed;
}

int satip_settle_config(t_satip_config* cfg)
{
  int i;
  int retval=SATIPCFG_OK;

  
  switch (cfg->status) 
    {
    case SATIPCFG_CHANGED:
    case SATIPCFG_PID_CHANGED:
      /* clear up addpids delpids */
      for ( i=0; i<SATIPCFG_MAX_PIDS; i++)
	if ( cfg->mod_pid[i] == PID_ADD )
	  cfg->mod_pid[i] = PID_VALID;
	else if (cfg->mod_pid[i] == PID_DELETE )
	  cfg->mod_pid[i] = PID_IGNORE;
      /* now settled */
      cfg->status = SATIPCFG_SETTLED;
      break;

    case SATIPCFG_SETTLED:
      break;

    case SATIPCFG_INCOMPLETE: /* cannot settle this.. */
    default:
      retval=SATIPCFG_ERROR;
      break;
    }

  return retval;
}


void satip_clear_config(t_satip_config* cfg)
{
  int i;

  cfg->status    = SATIPCFG_INCOMPLETE;
  cfg->param_cfg = 0;
  cfg->lnb_off = 0;
  
  for ( i=0; i<SATIPCFG_MAX_PIDS; i++)
    cfg->mod_pid[i]=PID_IGNORE;
}
