Virtualized DVB driver for kernels 4.16 ... 6.x 
from former project named "vtuner" by Honza Petrous.

Driver is registering the following devices:

DVB:

    /dev/dvb/adapterX/demux0
    /dev/dvb/adapterX/dvr0
    /dev/dvb/adapterX/frontend0

The frontend acts as "provider" for DVBS,DVBS2,DVBC and DVBT

Controlling:

    /dev/vtunerX

where X is ordered by driver installation.

It is possible to cat the received TS stream with

    cat /dev/vtunerX > stream.ts

