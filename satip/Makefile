CFLAGS += -Wall -Wextra -g

OBJ = satip_rtp.o satip_vtuner.o satip_config.o \
	satip_rtsp.o satip_main.o polltimer.o log.o
BIN = satip

$(BIN):  $(OBJ)
	$(CC) -o $(BIN) $(OBJ) -lrt -lpthread 

install:
	make $(BIN)
	cp $(BIN) /usr/local/bin

uninstall:
	rm /usr/local/bin/$(BIN)

clean:
	rm -f $(BIN) *.o *~
