CC = gcc
CFLAGS =  -g -O0
DEPS = 
OBJ = srvr.o cli.o device.o helloProtocol.o msgApi.o serialCmdSrv.o stateMachine.o tcpCmdClient.o tcpCmdSrv.o tcpConnSrv.o utils.o rnglib.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

srvr: $(OBJ)
	gcc -o $@ $^ $(CFLAGS)

clean: 
	rm *.o
	rm srvr
