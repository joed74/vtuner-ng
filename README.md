Patched linux kernel driver for kernels 3.10 ... 5.6 is part of virtualized DVB
adapter project named "vtuner" by Honza.

Driver is registering the following devices:

DVB:

    /dev/dvb/adapterX/demux0
    /dev/dvb/adapterX/dvr0
    /dev/dvb/adapterX/frontend0 [registered later]

Controlling:

    /dev/vtunerX

where X is ordered by driver installation.

