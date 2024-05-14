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

#ifndef _SATIP_RTP_H
#define _SATIP_RTP_H


typedef struct satip_rtp_last
{
  int signallevel;
  int quality;
} t_satip_rtp_last;

typedef struct satip_rtp
{
  int fd;
  int rtp_port;
  int rtp_socket;
  int rtcp_port;
  int rtcp_socket;
  unsigned char tune_id;
  t_satip_rtp_last last;
  pthread_t thread;
} t_satip_rtp;

struct satip_rtp*  satip_rtp_new(int fd, int fixed_rtp_port);
//int satip_rtp_port(struct satip_rtp* srtp);

#endif
