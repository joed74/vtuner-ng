Virtualized DVB driver for kernels 4.16 ... 6.x 
from former project named "vtuner" by Honza Petrous.

Driver is registering the following devices:

DVB:

    /dev/dvb/adapterX/ca0
    /dev/dvb/adapterX/demux0
    /dev/dvb/adapterX/dvr0
    /dev/dvb/adapterX/frontend0

The frontend acts as "provider" for DVBS,DVBS2,DVBC and DVBT

Controlling:

    /dev/vtunercX

where X is ordered by driver installation (e.g. /dev/vtunerc0).

It is possible to cat the received TS stream with

    cat /dev/vtunerc0 > stream.ts

