/*
 * satip: RTP processing
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sched.h>

#include "satip_rtp.h"
#include "log.h"

#include "vtuner.h"

typedef struct satip_rtp
{
  int fd;
  int rtp_port;
  int rtp_socket;
  int rtcp_port;
  int rtcp_socket;
  pthread_t thread;
} t_satip_rtp;


static void set_status(int fd, unsigned char status, unsigned short ss, unsigned int ber)
{
	struct vtuner_signal sig;
	sig.status = status;
	sig.ss = ss*255;
	sig.ber = ber*255;
	sig.snr = 0;
	sig.ucb = 0;
	ioctl(fd, VTUNER_SET_SIGNAL, &sig);
}

static void rtp_data(int fd, unsigned char* buffer,int rx)
{
  int done=0;
  uint32_t* buf=(uint32_t*) buffer;
  uint32_t val;
  int plen;
  int pt;
  char infobuf[200];

  unsigned int ber;
  unsigned short ss;
  unsigned char status;
  int freq;

  int update_status=0;

  while(done<rx)
    {
      val= ntohl(*buf);
      pt= ( val  & 0x00ff0000 ) >>16 ;
      plen= val & 0x0000ffff;

      switch(pt)
	{
	case 204:
	  if (plen>1)
	    {
	      val=buf[2];
	      DEBUG(MSG_DATA,"RTCP: app defined (204) name: %c%c%c%c\n",
		    val & 0x000000ff,
		    val>>8 & 0x000000ff,
		    val>>16 & 0x000000ff,
		    val>>24 & 0x000000ff);

	      val=htonl(buf[3]);
	      if ( val <200 )
		{
		  snprintf(infobuf,val,"%s",(char*) &buf[4]);
		  infobuf[val]=0;
		  DEBUG(MSG_DATA,"RTCP: app info: %s\n",infobuf);
		  char *token;
		  char *rest = infobuf;
		  int nr=0;
		  while ((token = strtok_r(rest, ",", &rest)))
		  {
			if (nr==1) {
				// Signal level 0-255
				int act_ss=atoi(token);
				if (act_ss!=ss) update_status=1;
				ss=act_ss;
			}
			if (nr==2) {
				// Fronend lock
				unsigned char act_status=FE_HAS_SIGNAL;
				if (token[0]=='1') act_status=FE_HAS_LOCK;
				if (act_status!=status) update_status=1;
				status=act_status;
			}
			if (nr==3) {
				// Quality (BER)
				int act_ber=atoi(token);
				if (act_ber!=ber) update_status=1;
				ber=act_ber;
			}
			if (nr==4) {
				// frequency
				int act_freq=atoi(token);
				if (act_freq!=freq) update_status=1;
				freq=act_freq;
			}
			nr++;
		  }
		}
	    }

	  break;

	default:
	  DEBUG(MSG_DATA,"RTCP: packet type %d len %d\n",pt,plen);
	}

      if (update_status) {
	      DEBUG(MSG_MAIN,"RTCP: update status=%i ss=%i ber=%i\n",status,ss,ber);
	      set_status(fd, status, ss, ber);
	      update_status=0;
      }

      buf+=plen+1;
      done+=(plen+1)*4;
    }
}

static void* rtp_receiver(void* param)
{
  unsigned char rxbuf[32768];
  struct pollfd pollfds[2];
  struct sched_param schedp;
  char filler[188];
  t_satip_rtp* srtp=(t_satip_rtp*)param;

  schedp.sched_priority = sched_get_priority_min(SCHED_FIFO)+1;

  if ( sched_setscheduler(0, SCHED_FIFO, &schedp) )
    DEBUG(MSG_MAIN,"RTP: No realtime scheduling\n");
  else
    DEBUG(MSG_MAIN,"RTP: Realtime scheduling enabled at prio %d\n",schedp.sched_priority);


  pollfds[0].fd = srtp->rtp_socket;
  pollfds[0].events = POLLIN;
  pollfds[0].revents = 0;

  pollfds[1].fd = srtp->rtcp_socket;
  pollfds[1].events = POLLIN;
  pollfds[1].revents = 0;

  memset(&filler,0xff,sizeof(filler));
  filler[0]=0x47;
  filler[1]=0x1F;
  filler[2]=0xFF;
  filler[3]=0x20; // only adaption, no payload
  filler[4]=0xB7; // adaption field length
  filler[5]=0x00; // adaption fields (none)

  while(1)
    {
      poll(pollfds,2,-1);

      if ( pollfds[0].revents & POLLIN )
	{
	  int rx,wr;
	  pollfds[0].revents = 0;

	  rx = recv(pollfds[0].fd, rxbuf, 32768,0);
	  DEBUG(MSG_DATA,"RTP: rd %d   wr %d\n",rx,wr);
	  if ( rx>12 && rxbuf[12] == 0x47 )
	    {
      		wr = write(srtp->fd,&rxbuf[12],rx-12);
		DEBUG(MSG_DATA,"RTP: rd %d  wr %d\n",rx,wr);
	    }
	    else
	    {
	        // send filler packet
                wr = write(srtp->fd,&filler,sizeof(filler));
	        DEBUG(MSG_DATA,"RTP: send filler %d\n",rx);
	    }
	}

      if ( pollfds[1].revents & POLLIN )
	{
	  int rx;
	  pollfds[1].revents = 0;

	  rx=recv(pollfds[1].fd, rxbuf, 32768,0);
	  rtp_data(srtp->fd, rxbuf,rx);
	  DEBUG(MSG_DATA,"RTCP: rd %d\n",rx);
	}

    }
  return NULL;
}



t_satip_rtp*  satip_rtp_new(int fd, int fixed_rtp_port)
{
  t_satip_rtp* srtp;
  int rtp_sock, rtcp_sock;
  int rtp_port, rtcp_port;
  struct timespec ts;
  int PORT_RANGE = 2000;
  int PORT_BASE = 45000;
  int attempts;

  if (fixed_rtp_port!=-1) {
     PORT_BASE = fixed_rtp_port;
     PORT_RANGE = 2;
  }

  attempts = PORT_RANGE/2;

  clock_gettime(CLOCK_REALTIME,&ts);

  srandom(ts.tv_nsec);

  while ( --attempts > 0 || PORT_RANGE == 2)
    {
      struct sockaddr_in inaddr;

      rtp_port= PORT_BASE + ( random()% (PORT_RANGE-1) );
      rtcp_port = rtp_port+1;

      rtp_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
      rtcp_sock= socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

      memset(&inaddr, 0, sizeof(inaddr));
      inaddr.sin_family = AF_INET;
      inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
      inaddr.sin_port = htons(rtp_port);

      if (bind(rtp_sock, (struct sockaddr *) &inaddr, sizeof(inaddr)) < 0) 
	{
	  close(rtp_sock);
	  close(rtcp_sock);
	  continue;
	}

#if 0
      {
	struct ip_mreq group;
	group.imr_multiaddr.s_addr = inet_addr("224.16.16.1");
	group.imr_interface.s_addr = inet_addr("192.168.178.57");
	if (setsockopt(rtp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       (char *)&group, sizeof(group)) < 0) {
	  close(rtp_sock);
	  close(rtcp_sock);
	  ERROR(MSG_NET,"error adding multicast group\n");
	  exit(1);
	}
      }
#endif


      memset(&inaddr, 0, sizeof(inaddr));
      inaddr.sin_family = AF_INET;
      inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
      inaddr.sin_port = htons(rtcp_port);

      if (bind(rtcp_sock, (struct sockaddr *) &inaddr, sizeof(inaddr)) < 0) 
	{
	  close(rtp_sock);
	  close(rtcp_sock);
	  continue;
	}

      INFO(MSG_NET,"rtp/rtcp port %d/%d\n",rtp_port,rtcp_port);
      break;
    }

  if (attempts <= 0 && PORT_RANGE > 2)
    return NULL;

  srtp=(t_satip_rtp*)malloc(sizeof(t_satip_rtp));

  srtp->fd = fd;
  srtp->rtp_port    = rtp_port;
  srtp->rtp_socket  = rtp_sock;
  srtp->rtcp_port   = rtcp_port;
  srtp->rtcp_socket = rtcp_sock;

  pthread_create( &srtp->thread, NULL, rtp_receiver, srtp);

  return srtp;
}


int satip_rtp_port(t_satip_rtp* srtp)
{
  return srtp->rtp_port;
}
