#include "replFsServer.h"
#include "protocol.h"

static void eventLoop();

extern char *mountdir;
extern int sock;
extern Sockaddr groupAddr;
extern uint32_t myId;
extern FileServer *meFileServer;

int main(int argc, char **argv) {

  if (argc != 7) {
    printf("Usage: replFsServer -port <portnum> -mount <mountdir> -drop <packetLoss>\n");
    exit(-1);
  }

  unsigned short port = atoi(argv[2]);
  mountdir = argv[4];
  int packetLoss = atoi(argv[6]);

  int retval = mkdir(mountdir, S_IRUSR | S_IWUSR | S_IXUSR);
  if (retval < 0) {
    fprintf(stderr, "machine already in use\n");
    exit(-1);
  }

  /* initialize the file server */
  netInit(port, packetLoss);
  myId = GenerateId();
  meFileServer = new FileServer(myId);

  /* start the event loop */
  eventLoop();

  return 0;
}

static void
eventLoop() {
  while (TRUE) {
    Event event;
    NextEvent(&event, sock);
    if (event.type == RECEIVED_PACKET) {
      ProcessPacketFileServer(&(event.packet));
    }
  }
}






