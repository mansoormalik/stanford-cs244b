
CFLAGS	= -g -Wall -DSUN
CC	= /usr/bin/g++
CCF	= $(CC) $(CFLAGS)

H	= .
C_DIR	= .

INCDIR	= -I$(H)
LIBDIRS = -L$(C_DIR)
LIBS    = -lclientReplFs 

CLIENT_OBJECTS = client.o protocol.o

all:	appl replFsServer

appl:	appl.o $(C_DIR)/libclientReplFs.a
	$(CCF) -o appl appl.o $(LIBDIRS) $(LIBS)

replFsServer:	replFsServer.o protocol.o
	$(CCF) -o replFsServer replFsServer.o protocol.o $(LIBDIRS) 

replFsServer.o:	replFsServer.c replFsServer.h
	$(CCF) -c $(INCDIR) replFsServer.c

protocol.o: protocol.c protocol.h
	$(CCF) -c $(INCDIR) protocol.c

appl.o:	appl.c client.h appl.h
	$(CCF) -c $(INCDIR) appl.c

$(C_DIR)/libclientReplFs.a:	$(CLIENT_OBJECTS)
	ar cr libclientReplFs.a $(CLIENT_OBJECTS)
	ranlib libclientReplFs.a

client.o:	client.c client.h
	$(CCF) -c $(INCDIR) client.c

clean:
	rm -f appl replFsServer *.o *.a *~

