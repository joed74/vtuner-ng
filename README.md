Virtualized DVB driver for kernels 3.10 ... 6.x 
From former project named "vtuner" by Honza Petrous.

Driver is registering the following devices:

DVB:

    /dev/dvb/adapterX/demux0
    /dev/dvb/adapterX/dvr0
    /dev/dvb/adapterX/frontend0 [registered later]

Controlling:

    /dev/vtunerX

where X is ordered by driver installation.

