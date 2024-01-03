/*
 * satip: startup and main loop
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
#include <sys/socket.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sched.h>
#include <signal.h>

#include "satip_config.h"
#include "satip_vtuner.h"
#include "satip_rtsp.h"
#include "satip_rtp.h"
#include "log.h"
#include "polltimer.h"

#include "vtuner.h"

int dbg_level = MSG_ERROR;
unsigned int dbg_mask = MSG_MAIN | MSG_NET; // MSG_DATA
int use_syslog = 0;
int abort_all = 0;
int test_sequencer = 0;
int test_counter = 0;

#define TEST_DVBS2 1

static void test_sequencer_loop(void* param)
{
  t_satip_config* sc=(t_satip_config*)param;

  test_counter++;
  
  switch (test_counter)
    {
    case 1:
      satip_set_position(sc, 1);
      satip_add_default_pids(sc);

#ifdef TEST_DVBS
      satip_set_dvbs(sc, 12188, SATIPCFG_P_HORIZONTAL, QPSK, 27500, SATIPCFG_F_34); // RTL
      satip_add_pid(sc, 163);
      satip_add_pid(sc, 104);
      satip_add_pid(sc, 106);
      satip_add_pid(sc, 110);
#endif

#ifdef TEST_DVBS2
      satip_set_dvbs2(sc, 11493, SATIPCFG_P_HORIZONTAL, PSK_8, 22000, SATIPCFG_F_23, SATIPCFG_R_0_35, SATIPCFG_P_AUTO); // Das Erste HD
      satip_add_pid(sc, 5101);
      satip_add_pid(sc, 5102);
      satip_add_pid(sc, 5103);
      satip_add_pid(sc, 5107);
      satip_add_pid(sc, 5106);
      satip_add_pid(sc, 5105);
#endif
      break;

    case 400:
      abort_all = 1;
      break;

    default: 
      break;
      
    }
}

static void enable_rt_scheduling()
{
  struct sched_param schedp;
  
  if ( mlockall(MCL_CURRENT|MCL_FUTURE) )    
    DEBUG(MSG_MAIN,"Pages not locked\n");
  else
    DEBUG(MSG_MAIN,"Pages locked\n");

  schedp.sched_priority = sched_get_priority_min(SCHED_FIFO);
  
  if ( sched_setscheduler(0, SCHED_FIFO, &schedp) )
    DEBUG(MSG_MAIN,"No realtime scheduling\n");
  else
    DEBUG(MSG_MAIN,"Realtime scheduling enabled at prio %d\n",schedp.sched_priority);
    
}


void hangup(int sig)
{
   UNUSED(sig);
   abort_all=1;
}

void usage(char *name)
{
  fprintf(stderr,
     "usage: %s -s satip_receiver [-p 554] [-d /dev/vtunercX] [-D delsys[,delsys]] [-f satip_frontend] [-l level] [-m mask] [-r fixed_rtp_port]\n"
     "  -s\tip or hostname of satip receiver\n"
     "  -p\tport of satip receiver (defaults to 554)\n"
     "  -d\tvtuner device (defaults to /dev/vtunerc0)\n"
     "  -D\tvtuner frontend delivery system, values: DVBS DVBS2 DVBT DVBT2 DVBC DVBC_B DVBC_C (defaults to all)\n"
     "  -f\tfrontend on satip receiver, number between 1 to N (defaults to let receiver decide)\n"
     "  -l\tloglevel: 1 = error, 2 = warnings, 3 = info, 4 = debug (defaults to error)\n"
     "  -m\tmask for logs: 1 = main, 2 = net, 4 = data, 7 = all (defaults to main + net)\n"
     "  -r\tfixed rtp port (e.g. 45200)\n"
     "  -T\ttest mode without vtuner, ts packets gets written to stdout!!\n"
     ,name
     );
}

int main(int argc, char** argv)
{
  char* host = NULL;
  char* port = "554";
  char* device = "/dev/vtunerc0";
  char* delsys = NULL;
  int frontend = -1;
  int fixed_rtp_port = -1;

  t_satip_config* satconf;
  struct satip_rtsp* srtsp;
  struct satip_rtp* srtp;
  struct satip_vtuner* satvt;

  struct pollfd pollfds[2];
  int poll_idx;
  struct polltimer* timerq=NULL;

  int opt;

  signal(SIGHUP, hangup);
  signal(SIGINT, hangup);
  signal(SIGTERM, hangup);

  while((opt = getopt(argc, argv, "s:Tp:d:D:f:m:l:r:h::")) != -1 ) {
    switch(opt) 
      {
      case 'h': 
	host = argv[optind];
	break;

      case 's':
	host = optarg;
	break;

      case 'p': 
	port = optarg;
	break;

      case 'd': 
	device = optarg;
	break;

      case 'D':
	delsys = optarg;
        break;

      case 'f': 
	frontend = atoi(optarg);
	break;	

      case 'm':
	dbg_mask = atoi(optarg);
	break;

      case 'l':
	dbg_level = atoi(optarg);
	break;

      case 'r':
	fixed_rtp_port = atoi(optarg);
	break;

      case 'T':
	test_sequencer = 1;
        break;

      default:
	exit(1);	
      }
  }

  if ( host==NULL ) {
    usage(argv[0]);
    exit(1);
  }
        

  enable_rt_scheduling();

  satconf = satip_new_config(frontend);

  if (test_sequencer) {

    struct polltimer_periodic* periodic;

    device = device;
    satvt = NULL;
    polltimer_periodic_start(&timerq,
			   &periodic,
			   test_sequencer_loop,
			   10,
			   (void*)satconf);
  
    srtp  = satip_rtp_new(1, fixed_rtp_port);
  
    /* no vtuner fd*/
    poll_idx=0;

  } else {

    satvt = satip_vtuner_new( device, delsys, satconf );
  
    if ( satvt == NULL )
      {
        fprintf(stderr,"cannot open %s\n", device);
        exit(1);
      }

    srtp  = satip_rtp_new(satip_vtuner_fd(satvt), fixed_rtp_port);

    pollfds[0].fd=satip_vtuner_fd(satvt);
    pollfds[0].events = POLLPRI;
    poll_idx=1;
  }

  srtsp = satip_rtsp_new(satconf,&timerq, host, port,
			 satip_rtp_port(srtp));
    

  while (1)
    {
      /* apply any updates on rtsp  */
      satip_rtsp_check_update(srtsp, abort_all);
      if (abort_all) exit(0);

      /* vt control events */
      pollfds[0].revents = 0;
      
      /* rtsp socket may be closed */
      pollfds[poll_idx].fd = satip_rtsp_socket(srtsp);
      pollfds[poll_idx].events = satip_rtsp_pollflags(srtsp);
      pollfds[poll_idx].revents = 0;
      
      /* poll and timeout on next pending timer */
      if ( poll(pollfds, pollfds[poll_idx].events == 0 ? poll_idx : poll_idx+1 , 
		polltimer_next_ms(timerq) ) ==-1 && 
	   errno!=EINTR )
	{
	  perror(NULL);
	  exit(1);
	}

      /* schedule timer callbacks */
      polltimer_call_next(&timerq);
	
      /* vt control event handling */
      if ( poll_idx>0 && pollfds[0].revents !=0 )
	satip_vtuner_event(satvt);

      /* rtsp event handling */
      if ( pollfds[poll_idx].revents !=0 ) 
	satip_rtsp_pollevents(srtsp, pollfds[poll_idx].revents);  
    }
  
  return 0;
  
}

