CFLAGS += -Wall -g

OBJ = satip_rtp.o satip_vtuner.o satip_config.o \
	satip_rtsp.o satip_main.o polltimer.o log.o
BIN = satip

$(BIN):  $(OBJ)
	$(CC) -o $(BIN) $(OBJ) -lrt -lpthread 

clean:
	rm -f $(BIN) *.o *~
