#include "protocol.h"



set<FileServer* > fileServers;
set<FileServer* > openFileResponses;
set<FileServer* > prepareToCommitResponses;
set<FileServer* > commitResponses;
set<FileServer* > abortResponses;
set<FileServer* > closeFileResponses;

list<WriteBlk* > uncommitedBlocks;
vector<uint32_t> missingBlocks;

Client *meClient;
FileServer *meFileServer;

uint32_t myId;
uint32_t currentFileDesc;

char *mountdir;
uint32_t currentTransactionId;

int sock;
Sockaddr groupAddr;

int PERCENT_DROPPED_PACKETS;

void Error(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(-1);
}

void
netInit(unsigned short port, int packetLoss)
{
  Sockaddr nullAddr;
  int reuse;
  u_char ttl;
  struct ip_mreq  mreq;

  /* initialize the pseudo-random number generator */
  srand((unsigned int)time(NULL));


  PERCENT_DROPPED_PACKETS = packetLoss;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    Error("error opening sock");

  /* SO_REUSEADDR allows more than one binding to the same
     sock - you cannot have more than one player on one
     machine without this */
  reuse = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0)
    Error("setsockopt failed (SO_REUSEADDR)");


  nullAddr.sin_family = AF_INET;
  nullAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  nullAddr.sin_port = port;
  if (bind(sock, (struct sockaddr *)&nullAddr,
           sizeof(nullAddr)) < 0)
    Error("failed to bind address");

  /* Multicast TTL:
     0 restricted to the same host
     1 restricted to the same subnet
     32 restricted to the same site
     64 restricted to the same region
     128 restricted to the same continent
     255 unrestricted

     DO NOT use a value > 32. If possible, use a value of 1 when
     testing.
  */

  ttl = 1;
  if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                 sizeof(ttl)) < 0)
    Error("setsockopt failed (IP_MULTICAST_TTL)");


  /* join the multicast group */
  mreq.imr_multiaddr.s_addr = htonl(MULTICAST_ADDR);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)
                 &mreq, sizeof(mreq)) < 0)
    Error("setsockopt failed (IP_ADD_MEMBERSHIP)");

  /* Get the multi-cast address ready to use in send calls. */
  memcpy(&groupAddr, &nullAddr, sizeof(Sockaddr));
  groupAddr.sin_addr.s_addr = htonl(MULTICAST_ADDR);

}


void CreatePacketHeader(Packet *outgoing, 
                        unsigned int messageType,
                        uint32_t destination) {
  outgoing->magicNumber = htonl(MAGIC_NUMBER);
  outgoing->version = htons(DRFS_VERSION);
  outgoing->type = messageType;
  outgoing->sourceId = htonl(myId);
  outgoing->destId = htonl(destination);
  outgoing->sequenceNumber = htonl(NextSequenceNumber());
}


void SendPacket(Packet *outgoing) {
  if (sendto(sock, outgoing, sizeof(Packet), 0,
             (struct sockaddr*) &groupAddr, sizeof(Sockaddr)) < 0) {
    Error("error sending packet");
  }
  if (DEBUG_DUMP_PACKETS) dumpPacket(outgoing, 1);
}


bool IsDropPacket() {
  // srand is invoked once in netInit
  static unsigned int const MAX_VALUE = 100;
  unsigned short randomValue = rand() % MAX_VALUE;
  if (randomValue > PERCENT_DROPPED_PACKETS) 
    return false;
  return true;
}


void HandleServerResponses(MessageType msgType, 
                           int *numAttempts, 
                           bool (IsTerminate())) {
  struct timeval timeBeforeLoop;
  long int maxTimeInLoopInMilliseconds = WAIT_TIME_IN_MSEC;
  long int elapsed;
  bool timedOut;
  Event event;
  
  /* used for explictly keeping track of timing */
  gettimeofday(&timeBeforeLoop, NULL);
  timedOut = false;
  while(!timedOut) {
    NextEvent(&event, sock);
    if ( (event.type == RECEIVED_PACKET) &&
         (event.packet.type == msgType) ) {
      ProcessPacketClient(&event.packet);
      
      if (IsTerminate()) {
        timedOut = true;
        *numAttempts = MAX_ATTEMPTS;
      }
    }
    elapsed = TimeElapsedInMilliseconds(&timeBeforeLoop);
    if (elapsed > maxTimeInLoopInMilliseconds) {
      timedOut = true;
    }
  }
}


void
SendJoinRequest(int numServers) {
  int numRequests = 0;
  bool isDuplicateIdDetected = false;

  /* called by the client only */
  myId = GenerateId();
  meClient = new Client(myId);

  while(numRequests < MAX_ATTEMPTS) {

    Packet packet;
    CreatePacketHeader(&packet,
                       JOIN_REQUEST,
                       htonl(BROADCAST_ID));

    JoinRequest jrq;
    if (isDuplicateIdDetected) {
      isDuplicateIdDetected = false;
      jrq.requestCode = RESPOND_WITH_NEW_ID;
    } else {
      jrq.requestCode = RESPOND_WITH_EXISTING_ID;
    }
    memcpy(&packet.body, &jrq, sizeof(JoinRequest));

    SendPacket(&packet);
    numRequests++;

    /* used for select call */
    fd_set fdmask;
    struct timeval timeout;
    int retval;
    timeout.tv_sec = WAIT_TIME_IN_SEC;
    timeout.tv_usec = 0;

    FD_ZERO(&fdmask);
    FD_SET(sock, &fdmask);
    
    /* used for explictly keeping track of timing */
    struct timeval timeBeforeLoop;
    gettimeofday(&timeBeforeLoop, NULL);
    long int maxTimeInLoopInMilliseconds = (timeout.tv_sec) * 1000; 

    bool timedOut = false;
    while (!timedOut) {
      retval = select(sock+1, &fdmask, NULL, NULL, &timeout);
      if (retval == 0) {
        timedOut = true;
      } else if (retval > 0) {

        /* we may never be timed out if the socket keeps getting
           messages from other nodes so check timing 
        */
        long int elapsed = TimeElapsedInMilliseconds(&timeBeforeLoop);
        if (elapsed > maxTimeInLoopInMilliseconds) {
          timedOut = true;
          continue;
        }

        /* got a packet on this socket but need to check that it is for us */
        Packet incoming;
        Sockaddr source;
        socklen_t sourceLen = sizeof(source);
        retval = recvfrom(sock, (char *)&incoming,
                          sizeof(Packet), 0,
                          (struct sockaddr *)&source,
                          &sourceLen);
        
        if (retval <= 0) {
          printf("error receiving packet in JoinRequest\n"); 
        } else {         

          if ( (IsPacketValid(&incoming)) &&
               (IsDestAddrMine(&incoming)) &&
               (incoming.type == JOIN_RESPONSE) ) {

            if (DEBUG_DUMP_PACKETS) dumpPacket(&incoming, 0);
            
            int sourceId = ntohl(incoming.sourceId);

            if (IsUnknownId(sourceId)) {
              FileServer *fs = new FileServer(sourceId);
              fs->source( &source );
              fileServers.insert(fs);
              if ((int)fileServers.size() == numServers) {
                return;
              }
            } else if (IsDuplicateId(sourceId, &source)) {
              isDuplicateIdDetected = true;
              ClearFileServers();
              numRequests = 0;
              timedOut = 0;
            }
          }
        }
      }
    }
  }
  
  /* we'll get to this point if the number of responses did not match
     the number of file servers */
  Error("ERROR: number of JoinResponses did not match number of file servers\n");
}

/* This function implements the DRFS protocol for nodes
   that are trying to join. We need to check that the
   new node is not using a duplicate id.
*/

void 
RecvJoinRequest(Packet *incoming) {

  JoinRequest jrq;
  memcpy(&jrq, incoming->body, sizeof(jrq));
  if (jrq.requestCode == RESPOND_WITH_NEW_ID) {
    myId = GenerateId();
    meFileServer->id( myId );
  }

  /* Send response */
  Packet outgoing;
  CreatePacketHeader(&outgoing, 
                     JOIN_RESPONSE,
                     ntohl(incoming->sourceId));
  SendPacket(&outgoing);
}


void SendOpenFileRequest(char *filename) {
  if (strlen(filename) > (int)MaxFileNameLength) {
    fprintf(stderr, "Error: maximum file length exceeded in SendOpenFileRequest\n");
    return;
  }
  Packet outgoing;
  OpenFileRequest ofr;
  strcpy((char *)&(ofr.filename), filename);

  memcpy(&(outgoing.body), &ofr, sizeof(OpenFileRequest));

  CreatePacketHeader(&outgoing, OPEN_FILE_REQUEST, BROADCAST_ID);
  SendPacket(&outgoing);
}


void RecvOpenFileRequest(Packet *incoming) {
  OpenFileRequest ofrq;
  memcpy(&ofrq, &(incoming->body), sizeof(OpenFileRequest));

  char path[256];
  bzero(path, sizeof(path));
  strcpy(path, mountdir);
  strcat(path, "/");
  strcat(path, ofrq.filename);

  /* address of sender will be the destination of our response */
  unsigned int destId = ntohl(incoming->sourceId);

  /* check if this is an existing file and is already open */
  if (meFileServer->isFileOpen()) {
    if (strcmp(meFileServer->filename().c_str(), path) == 0) {
      SendOpenFileResponse(meFileServer->remoteFileDesc(), destId);
    } else {
      /* client can only open one file at a time */
      SendOpenFileResponse(INVALID_FD, destId);
    }
  } else {

    int remoteFileDesc = NextFileDescriptor();
    meFileServer->isFileOpen( true );
    meFileServer->remoteFileDesc( remoteFileDesc );
    meFileServer->filename( string(path) );

    SendOpenFileResponse(remoteFileDesc, destId);
  }
}


void SendOpenFileResponse(int filedesc, uint32_t destId) {
  Packet outgoing;
  OpenFileResponse ofrs;
  ofrs.fileDesc = htonl(filedesc);
  memcpy(&(outgoing.body), &ofrs, sizeof(OpenFileResponse));

  CreatePacketHeader(&outgoing, OPEN_FILE_RESPONSE, destId);
  SendPacket(&outgoing);
}

void RecvOpenFileResponse(Packet *incoming) {
  OpenFileResponse ofrs;
  memcpy(&ofrs, &(incoming->body), sizeof(OpenFileResponse));

  FileServer *fs = LookupFileServer( ntohl(incoming->sourceId) );
  if (fs != NULL) {
    fs->isFileOpen( true );
    fs->remoteFileDesc( ntohl(ofrs.fileDesc) );
    openFileResponses.insert(fs);
  }
}

void SendWriteBlock(WriteBlk *wb) {
  Packet outgoing;
  memcpy(&(outgoing.body), wb, sizeof(WriteBlk));

  CreatePacketHeader(&outgoing, WRITE_BLOCK, BROADCAST_ID);
  SendPacket(&outgoing);
}

void RecvWriteBlock(Packet *incoming) {
  WriteBlk *wb = new WriteBlk;
  memcpy(wb, incoming->body, sizeof(WriteBlk));
  if (meFileServer->remoteFileDesc() != ntohl(wb->fileDesc)) {
    delete wb;
    return;
  } else {
    /* check if we received this write block previously */
    if (LookupUncommitedBlock( ntohl(wb->blockId) ) != NULL) {
      delete wb;
      return;
    }
    uncommitedBlocks.push_back(wb);
  }
}

void SendPrepareToCommitRequest(uint32_t transactionId,
                                uint32_t fileDesc,
                                uint32_t firstBlockId,
                                uint32_t lastBlockId) {

  PrepareToCommitRequest ptcr;
  ptcr.transactionId = htonl(transactionId);
  ptcr.fileDesc = htonl(fileDesc);
  ptcr.firstBlockId = htonl(firstBlockId);
  ptcr.lastBlockId = htonl(lastBlockId);

  Packet outgoing;
  memcpy(&(outgoing.body), &ptcr, sizeof(ptcr));
  CreatePacketHeader(&outgoing, PREPARE_TO_COMMIT_REQUEST, BROADCAST_ID);
  SendPacket(&outgoing);
}

void RecvPrepareToCommitRequest(Packet *incoming) {
  PrepareToCommitRequest ptcr;
  memcpy(&ptcr, incoming->body, sizeof(ptcr));
  
  /* check that the file descriptor is valid */
  if (meFileServer->remoteFileDesc() != ntohl( ptcr.fileDesc )) {
    SendPrepareToCommitResponse(ntohl(ptcr.transactionId),
                                VOTE_NO_INVALID_FILE_DESC,
                                ntohl(incoming->sourceId));;
    return;
  }

  meFileServer->transactionId( ntohl(ptcr.transactionId) );

  if (!IsBlockMissing(ntohl(ptcr.firstBlockId), 
                      ntohl(ptcr.lastBlockId))) {
    SendPrepareToCommitResponse(ntohl(ptcr.transactionId), 
                                VOTE_YES, 
                                ntohl(incoming->sourceId));
    return;
  } else {
    SendPrepareToCommitResponse(ntohl(ptcr.transactionId),
                                VOTE_NO_MISSING_BLOCKS,
                                ntohl(incoming->sourceId));;
    return;
  }
}

void SendPrepareToCommitResponse(uint32_t transactionId,
                                 PrepareToCommitResponseCode rc,
                                 uint32_t destId) {
  Packet outgoing;
  CreatePacketHeader(&outgoing, PREPARE_TO_COMMIT_RESPONSE, destId);

  PrepareToCommitResponse ptcr;
  ptcr.transactionId = htonl(transactionId);
  ptcr.responseCode = rc;
  if (rc == VOTE_YES) {
    ptcr.numMissingBlocks = htonl(0);
  } else if (rc == VOTE_NO_MISSING_BLOCKS) {
    ptcr.numMissingBlocks = htonl( missingBlocks.size() );
    for (unsigned int i = 0; i < missingBlocks.size(); i++) {
      int blockId = missingBlocks.at(i);
      ptcr.missingBlockId[i] = htonl(blockId);
    }

  }
  memcpy(&(outgoing.body), &ptcr, sizeof(ptcr));
  SendPacket(&outgoing);
}

void RecvPrepareToCommitResponse(Packet *incoming) {
  PrepareToCommitResponse ptcr;
  memcpy(&ptcr, incoming->body, sizeof(ptcr));

  FileServer *fs = LookupFileServer( ntohl(incoming->sourceId) );
  if (fs == NULL) { 
    printf("Error: could not locate file server %d in RecvPrepareToCommitResponse\n", 
           ntohl(incoming->sourceId));
    return;
  }
  
  if (ptcr.responseCode == VOTE_YES) {
    fs->prepareToCommitResponseCode( VOTE_YES );
    prepareToCommitResponses.insert(fs);
  } else if (ptcr.responseCode == VOTE_NO_MISSING_BLOCKS) {
    for (unsigned int i = 0; i < ntohl(ptcr.numMissingBlocks); i++) {
      missingBlocks.push_back( ntohl( ptcr.missingBlockId[i] ) );
    }
  } else if (ptcr.responseCode == VOTE_NO_INVALID_FILE_DESC) {
    fs->prepareToCommitResponseCode( VOTE_NO_INVALID_FILE_DESC );
    prepareToCommitResponses.insert(fs);
  } 
}

void SendCommitRequest(uint32_t transactionId) {
  CommitRequest crq;
  crq.transactionId = htonl(transactionId);
  
  Packet outgoing;
  memcpy(&(outgoing.body), &crq, sizeof(crq));
  CreatePacketHeader(&outgoing, COMMIT_REQUEST, BROADCAST_ID);
  SendPacket(&outgoing);
}

void RecvCommitRequest(Packet *incoming) {
  CommitRequest crq;
  memcpy(&crq, incoming->body, sizeof(crq));

  /* check that the transaction id is valid */
  if ( ntohl(crq.transactionId) != meFileServer->transactionId()) {
    fprintf(stderr, "Error: invalid transaction id in RecvCommitRequest\n");
    return;
  }

  if (!(meFileServer->isValidLocalFileDesc())) {
    int localFileDesc = open(meFileServer->filename().c_str(), 
                             O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
    if (localFileDesc < 0) 
      Error("Could not open file.\n");
    meFileServer->localFileDesc( localFileDesc );
    meFileServer->isValidLocalFileDesc( true );
  }

  /* write uncommited blocks in ascending order based on block id */
  uncommitedBlocks.sort(BlockCompare);
  int fd = meFileServer->localFileDesc();
  for (list<WriteBlk*>::iterator it=uncommitedBlocks.begin();
       it != uncommitedBlocks.end();
       it++) {
    WriteBlk *wb = *it;
    lseek(fd, ntohl(wb->byteOffset), SEEK_SET);
    write(fd, wb->buffer, ntohl(wb->blockSize));
  }

  ClearUncommitedBlocks();
  SendCommitResponse( ntohl(crq.transactionId), ntohl(incoming->destId) );
}    


void SendCommitResponse(uint32_t transactionId, 
                        uint32_t destId) {
  CommitResponse crs;
  crs.transactionId = htonl(transactionId);
  
  Packet outgoing;
  memcpy(&(outgoing.body), &crs, sizeof(crs));
  CreatePacketHeader(&outgoing, COMMIT_RESPONSE, destId);
  SendPacket(&outgoing);
}

void RecvCommitResponse(Packet *incoming) {
  CommitResponse crs;
  memcpy(&crs, incoming->body, sizeof(crs));

  FileServer *fs = LookupFileServer( ntohl(incoming->sourceId) );
  if (fs != NULL)
    commitResponses.insert(fs);
}


void SendAbortRequest(uint32_t fileDesc) {
  AbortRequest arq;
  arq.fileDesc = htonl(fileDesc);
  
  Packet outgoing;
  memcpy(&(outgoing.body), &arq, sizeof(arq));
  CreatePacketHeader(&outgoing, ABORT_REQUEST, BROADCAST_ID);
  SendPacket(&outgoing);
}

void RecvAbortRequest(Packet *incoming) {
  AbortRequest arq;
  memcpy(&arq, &(incoming->body), sizeof(arq));
  
  if (meFileServer->remoteFileDesc() == (int)ntohl(arq.fileDesc) ) {
    ClearUncommitedBlocks();
    SendAbortResponse(ABORT_SUCCESS, ntohl(incoming->destId) );
  } else {
    /* the file descriptor did not match */
    SendAbortResponse(ABORT_FAILURE, ntohl(incoming->destId) );
  }
}

void SendAbortResponse(AbortResponseCode rc, uint32_t destId) {
  AbortResponse ars;
  ars.responseCode = rc;
  
  Packet outgoing;
  memcpy(&(outgoing.body), &ars, sizeof(ars));
  CreatePacketHeader(&outgoing, ABORT_RESPONSE, destId);
  SendPacket(&outgoing);
}

void RecvAbortResponse(Packet *incoming) {
  AbortResponse ars;
  memcpy(&ars, incoming->body, sizeof(ars));
  FileServer *fs = LookupFileServer( ntohl(incoming->sourceId) );
  if (fs != NULL) {
    fs->abortResponseCode( (AbortResponseCode)ars.responseCode );
    abortResponses.insert(fs);
  }
}

void SendCloseFileRequest(uint32_t fileDesc){

  CloseFileRequest cfrq;
  cfrq.fileDesc = htonl(fileDesc);
  
  Packet outgoing;
  memcpy(&(outgoing.body), &cfrq, sizeof(cfrq));
  CreatePacketHeader(&outgoing, CLOSE_FILE_REQUEST, BROADCAST_ID);
  SendPacket(&outgoing);
}

void RecvCloseFileRequest(Packet *incoming) {
  CloseFileRequest cfrq;
  memcpy(&cfrq, incoming->body, sizeof(cfrq));

  /* check that the file descriptors match */
  if (meFileServer->remoteFileDesc() != (int)ntohl(cfrq.fileDesc) ) {
    SendCloseFileResponse(CLOSE_FAILURE, ntohl(incoming->sourceId));
    return;
  }

  /* don't close the file multiple times */
  if ( meFileServer->isFileOpen() &&
       meFileServer->remoteFileDesc() == (int)ntohl(cfrq.fileDesc) ) {
    close( meFileServer->localFileDesc() );
    meFileServer->isFileOpen( false );
    meFileServer->isValidLocalFileDesc( false );
  }
  SendCloseFileResponse(CLOSE_SUCCESS, ntohl(incoming->sourceId));
}

void SendCloseFileResponse(CloseFileResponseCode rc,
                           uint32_t destId) {
  CloseFileResponse cfrs;
  cfrs.responseCode = rc;
  
  Packet outgoing;
  memcpy(&(outgoing.body), &cfrs, sizeof(cfrs));
  CreatePacketHeader(&outgoing, CLOSE_FILE_RESPONSE, destId);
  SendPacket(&outgoing);
}

void RecvCloseFileResponse(Packet *incoming) {
  CloseFileResponse cfrs;
  memcpy(&cfrs, incoming->body, sizeof(cfrs));

  if (cfrs.responseCode == CLOSE_SUCCESS) {
    FileServer *fs = LookupFileServer( ntohl(incoming->sourceId) );
    if (fs != NULL)
      fs->isFileOpen( false );
      closeFileResponses.insert(fs);
  }

}

/* This function generates a random number from 1 to
   RAND_MAX. The value of RAND_MAX is platform specific.
   On a 32-bit Linux platform, this value is 2^31.
 */
int GenerateId() {
  struct timeval now;
  gettimeofday(&now, NULL);
  srand(now.tv_usec);
  while(true) {
    int id = rand();
    if (id != 0) 
      return id;
  }
}


void 
dumpPacket(Packet *packet, int isSending) {

  const char *header = "%s msgType: %s srcId: %u dstId: %u ";
  char pktDirection[][5] = { "RECV", "SEND" };
  char messageType[][32] = { "JoinRequest", "JoinResponse", "OpenFileRequest",
                             "OpenFileResponse", "WriteBlock", "PrepareToCommitRequest",
                             "PrepareToCommitResponse", "CommitRequest", "CommitResponse",
                             "AbortRequest", "AbortResponse", "CloseFileRequest", 
                             "CloseFileResponse", "Heartbeat" };
  char buf[768];
  sprintf(buf, 
          header,
          pktDirection[isSending],
          messageType[packet->type],
          htonl(packet->sourceId), 
          htonl(packet->destId));
  printf("%s", buf);

  bzero(buf, sizeof(buf));
  switch(packet->type) {
  case JOIN_REQUEST: {
    char requestCode[][32] = { "RespondWithExistingId", "RespondWithNewId" };
    JoinRequest jrq;
    memcpy(&jrq, packet->body, sizeof(JoinRequest));
    const char *msg = "requestType: %s\n";
    sprintf(buf, msg, requestCode[jrq.requestCode]);
    printf("%s", buf);
    break;
  }
  case JOIN_RESPONSE: {
    printf("\n");
    break;
  }
  case OPEN_FILE_REQUEST: {
    OpenFileRequest ofrq;
    memcpy(&ofrq, packet->body, sizeof(OpenFileRequest));
    const char *msg = "filename: %s \n";
    sprintf(buf, msg, ofrq.filename);
    printf("%s", buf);
    break;
  }
  case OPEN_FILE_RESPONSE: {
    OpenFileResponse ofrs;
    memcpy(&ofrs, packet->body, sizeof(OpenFileResponse));
    const char *msg = "file descriptor: %d \n";
    sprintf(buf, msg, ntohl(ofrs.fileDesc));
    printf("%s", buf);
    break;
  }
  case WRITE_BLOCK: {
    WriteBlk wb;
    memcpy(&wb, packet->body, sizeof(WriteBlk));
    /*
    const char *msg = 
      "blockId: %d fileDesc: %d, byteOffset: %d, blockSize: %d, buffer: %s\n";
    sprintf(buf, msg, ntohl(wb.blockId), ntohl(wb.fileDesc), ntohl(wb.byteOffset),
            ntohl(wb.blockSize), wb.buffer);
    */
    const char *msg = 
      "blockId: %d fileDesc: %d, byteOffset: %d, blockSize: %d\n";
    sprintf(buf, msg, ntohl(wb.blockId), ntohl(wb.fileDesc), ntohl(wb.byteOffset),
            ntohl(wb.blockSize));
    printf("%s", buf);
    break;
  }
  case PREPARE_TO_COMMIT_REQUEST: {
    PrepareToCommitRequest ptcr;
    memcpy(&ptcr, packet->body, sizeof(ptcr));
    const char *msg = 
      "transactionId: %d fileDesc: %d, firstBlockId: %d, lastBlockId: %d\n";
    sprintf(buf, msg, ntohl(ptcr.transactionId), ntohl(ptcr.fileDesc),
            ntohl(ptcr.firstBlockId), ntohl(ptcr.lastBlockId));
    printf("%s", buf);
    break;
  }
  case PREPARE_TO_COMMIT_RESPONSE: {
    char responseCode[][32] = { "VOTE_YES",
                                "VOTE_NO_MISSING_BLOCKS",
                                "VOTE_NO_INVALID_FILE_DESC" };
    PrepareToCommitResponse ptcr;
    memcpy(&ptcr, packet->body, sizeof(ptcr));
    const char *msg = 
      "transactionId: %d responseCode: %s, numMissingBlocks: %d missingBlockId: ";
    sprintf(buf, msg, ntohl(ptcr.transactionId), responseCode[ptcr.responseCode],
            ntohl(ptcr.numMissingBlocks));
    printf("%s", buf);
    for (unsigned int i = 0; i < missingBlocks.size(); i++) 
      printf("%d ", missingBlocks.at(i));
    printf("\n");
    break;
  }
  case COMMIT_REQUEST: {
    CommitRequest crq;
    memcpy(&crq, packet->body, sizeof(crq));
    const char *msg = "transactionId: %d\n";
    sprintf(buf, msg, ntohl(crq.transactionId));
    printf("%s", buf);
    break;
  }
  case COMMIT_RESPONSE: {
    CommitRequest crs;
    memcpy(&crs, packet->body, sizeof(crs));
    const char *msg = "transactionId: %d\n";
    sprintf(buf, msg, ntohl(crs.transactionId));
    printf("%s", buf);
    break;
  }
  case ABORT_REQUEST: {
    AbortRequest arq;
    memcpy(&arq, packet->body, sizeof(arq));
    const char *msg = "fileDesc: %d\n";
    sprintf(buf, msg, ntohl(arq.fileDesc));
    printf("%s", buf);
    break;
  }
  case ABORT_RESPONSE: {
    char responseCode[][32] = { "ABORT_SUCCESS", "ABORT_FAILURE" };
    AbortResponse ars;
    memcpy(&ars, packet->body, sizeof(ars));
    const char *msg = "responseCode: %s\n";
    sprintf(buf, msg, responseCode[ars.responseCode]);
    printf("%s", buf);
    break;
  }
  case CLOSE_FILE_REQUEST: {
    CloseFileRequest cfrq;
    memcpy(&cfrq, packet->body, sizeof(cfrq));
    const char *msg = "fileDesc: %d\n";
    sprintf(buf, msg, ntohl(cfrq.fileDesc));
    printf("%s", buf);
    break;
  }
  case CLOSE_FILE_RESPONSE: {
    char responseCode[][32] = { "CLOSE_SUCCESS", "CLOSE_FAILURE" };
    CloseFileResponse cfrs;
    memcpy(&cfrs, packet->body, sizeof(cfrs));
    const char *msg = "responseCode: %s\n";
    sprintf(buf, msg, responseCode[cfrs.responseCode]);
    printf("%s", buf);
    break;
  }
  }
}

/* This function examines a packet and returns false if a any of
   the following conditions are met:
   (a) the packet was sent out by our own node
   (b) the magic number is invalid
   (c) the DRFS version number is invalid
   (d) the message type is invalid
 */
bool 
IsPacketValid (Packet *packet) {
  if (ntohl(packet->sourceId) == myId) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: sent out by me\n");
    return false;
  }

  if (ntohl(packet->magicNumber) != MAGIC_NUMBER) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect magic number\n");
    return false;
  }

  if (ntohs(packet->version) != DRFS_VERSION) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect DRFS version\n");
    return false;
  }

  if ( ((int)packet->type < JOIN_REQUEST) ||
       ((int)packet->type > CLOSE_FILE_RESPONSE) ) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect message type\n");
    return false;
  }
  return true;
}


bool 
IsDestAddrMine (Packet *packet) {
  if (ntohl(packet->destId) == myId) 
    return true;
  return false;
}

/* This function takes a timestamp as a parameter and returns
   the number of milliseconds that have elapsed since the timestamp
   was taken.
 */
long int TimeElapsedInMilliseconds(struct timeval *timestamp) {
  struct timeval now;
  gettimeofday(&now, NULL);
  long int timeElapsedInMilliseconds =
    ( ((now.tv_sec - timestamp->tv_sec) * 1000) + 
      ((now.tv_usec - timestamp->tv_usec) / 1000) );
  return timeElapsedInMilliseconds;
}


unsigned int NextSequenceNumber() {
  static unsigned int sn = 0;
  return ++sn;
}

unsigned int NextFileDescriptor() {
  static unsigned int fd = 0;
  return ++fd;
}

/* This function will detect duplicate IDs if
   file servers are running on different machines.
 */
bool IsDuplicateId(unsigned int id, Sockaddr *source) {
  if (id == myId)
    return true;

  for (set<FileServer *>::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (id == fs->id()) {
      if ( (fs->source()->sin_addr.s_addr != source->sin_addr.s_addr) ||
           (fs->source()->sin_port != source->sin_port) )
        return true;

    }
  }
  return false;
}


bool IsUnknownId(unsigned int id) {
  for (set<FileServer *>::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (id == fs->id())
      return false;
  }
  return true;
}

void ProcessPacketFileServer(Packet *incoming) {

  if (!IsPacketValid(incoming)) return;

  if (IsDropPacket()) {
    if (DEBUG_DUMP_PACKETS) {
      printf("DROP ");
      dumpPacket(incoming, 0);
    }
    return;
  } else {
    if (DEBUG_DUMP_PACKETS) dumpPacket(incoming, 0);
  }

  if (incoming->type == JOIN_REQUEST) {
    RecvJoinRequest(incoming);
  } else if (incoming->type == OPEN_FILE_REQUEST) {
    RecvOpenFileRequest(incoming);
  } else if (incoming->type == WRITE_BLOCK) {
    RecvWriteBlock(incoming);
  } else if (incoming->type == PREPARE_TO_COMMIT_REQUEST) {
    RecvPrepareToCommitRequest(incoming);
  } else if (incoming->type == COMMIT_REQUEST) {
    RecvCommitRequest(incoming);
  } else if (incoming->type == ABORT_REQUEST) {
    RecvAbortRequest(incoming);
  } else if (incoming->type == CLOSE_FILE_REQUEST) {
    RecvCloseFileRequest(incoming);
  }

}

void ProcessPacketClient(Packet *incoming) {

  if (!IsPacketValid(incoming)) return;

  if (IsDropPacket()) {
    if (DEBUG_DUMP_PACKETS) {
      printf("DROP ");
      dumpPacket(incoming, 0);
    }
    return;
  } else {
    if (DEBUG_DUMP_PACKETS) dumpPacket(incoming, 0);
  }
  
  if (incoming->type == OPEN_FILE_RESPONSE) {
    RecvOpenFileResponse(incoming);
  } else if (incoming->type == PREPARE_TO_COMMIT_RESPONSE) {
    RecvPrepareToCommitResponse(incoming);
  } else if (incoming->type == COMMIT_RESPONSE) {
    RecvCommitResponse(incoming);
  } else if (incoming->type == ABORT_RESPONSE) {
    RecvAbortResponse(incoming);
  } else if (incoming->type == CLOSE_FILE_RESPONSE) {
    RecvCloseFileResponse(incoming);
  }

}


void
NextEvent(Event *event, int sock) {

  struct timeval timeout;
  timeout.tv_sec = WAIT_TIME_IN_SEC;
  timeout.tv_usec = 0;
  
  fd_set fdmask;
  FD_ZERO(&fdmask);
  FD_SET(sock, &fdmask);

  int retval = select(sock + 1, &fdmask, NULL, NULL, &timeout);
  if (retval == 0) {
    event->type = TIMED_OUT;
  } else {
    event->type = RECEIVED_PACKET;
    Packet incoming;
    Sockaddr source;
    socklen_t sourceLen = sizeof(source);
    retval = recvfrom(sock, (char *)&incoming,
                      sizeof(Packet), 0,
                      (struct sockaddr *)&source,
                      &sourceLen);

    if (retval <= 0) {
      printf("error receiving packet\n");
    } else {
      memcpy(&(event->packet), &incoming, sizeof(incoming));
    }

  }

}


bool IsDoneRecvOpenFileResponses() {
  if (openFileResponses.size() < fileServers.size()) 
    return false;

  /* make sure all the file descriptors from the servers are the same */
  int firstFileServer = true;
  int lastFileDesc;
  for (set<FileServer *>::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (firstFileServer) {
      firstFileServer = false;
      lastFileDesc = fs->remoteFileDesc();
    } else {
      if (lastFileDesc != (int)fs->remoteFileDesc()) 
        return false;
      lastFileDesc = fs->remoteFileDesc();
    }
  }
  currentFileDesc = lastFileDesc;
  return true;
}


bool IsDoneRecvPrepareToCommitResponses() {
  if (prepareToCommitResponses.size() < fileServers.size()) 
    return false;
  return true;
}

bool IsDoneRecvCommitResponses() {
  if (commitResponses.size() < fileServers.size()) 
    return false;
  return true;
}

bool IsDoneRecvCloseFileResponses() {
  if (closeFileResponses.size() < fileServers.size()) 
    return false;
  return true;
}


bool IsAllFileServersVoteYes() {
  if (prepareToCommitResponses.size() < fileServers.size()) 
    return false;
  for (set<FileServer *>::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (fs->prepareToCommitResponseCode() != VOTE_YES) {
      return false;
    }
  }
  return true;
}

bool IsAllFileServersCommited() {
  if (commitResponses.size() < fileServers.size()) 
    return false;
  return true;
}

bool IsReceivedAllAbortResponses() {
  if (abortResponses.size() < fileServers.size())
    return false;
  return true;
}

bool IsAbortSuccessful() {
  if (abortResponses.size() < fileServers.size())
    return false;
  for (set<FileServer* >::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (fs->abortResponseCode() != ABORT_SUCCESS)
      return false;
  }
  return true;
}


bool IsBlockMissing(uint32_t firstBlockId, uint32_t lastBlockId) {
  /* special case if client tries to commit when there have been no updates */
  if ( ((int)firstBlockId == -1) && ((int)lastBlockId == -1) )
    return false;

  missingBlocks.clear();
  for (unsigned int i = firstBlockId; i <= lastBlockId; i++) {
    WriteBlk *wb = LookupUncommitedBlock(i);
    if (wb == NULL)
      missingBlocks.push_back(i);
  }
  if (missingBlocks.size() == 0) 
    return false;
  return true;
}

bool IsValidLocalFileDesc(int fd) {
  return false;
}


uint32_t FirstUncommitedBlockId() {
  if (uncommitedBlocks.size() == 0) return -1;
  WriteBlk *wb = uncommitedBlocks.front();
  return ( ntohl(wb->blockId) );
}

uint32_t LastUncommitedBlockId() {
  if (uncommitedBlocks.size() == 0) return -1;
  WriteBlk *wb = NULL;
  for (list<WriteBlk*>::iterator it=uncommitedBlocks.begin();
       it != uncommitedBlocks.end();
       it++) {
    wb = *it;
  }
  return ( ntohl(wb->blockId) );
}

uint32_t
NextBlockId() {
  static uint32_t id = 0;
  return ++id;
}

uint32_t
NextTransactionId() {
  static uint32_t id = 0;
  return ++id;
}

FileServer* LookupFileServer(uint32_t id) {
  for (set<FileServer *>::iterator it = fileServers.begin();
       it != fileServers.end();
       it++) {
    FileServer *fs = *it;
    if (fs->id() == id)
      return fs;
  }
  return NULL;
}

WriteBlk* LookupUncommitedBlock(uint32_t blockId) {
  for (list<WriteBlk *>::iterator it = uncommitedBlocks.begin();
       it != uncommitedBlocks.end();
       it++) {
    WriteBlk *wb = *it;
    if (blockId == ntohl(wb->blockId))
      return wb;
  }
  return NULL;
}

bool BlockCompare(WriteBlk *first, WriteBlk *second) {
  return (ntohl(first->blockId)  < ntohl(second->blockId));
}

void ClearUncommitedBlocks() {
  for (list<WriteBlk* >::iterator it = uncommitedBlocks.begin();
       it != uncommitedBlocks.end(); 
       it++) {
    WriteBlk *wb = *it;
    delete wb;
  }
  uncommitedBlocks.clear();
}

void ClearFileServers() {
  for (set<FileServer* >::iterator it = fileServers.begin();
       it != fileServers.end(); 
       it++) {
    FileServer *fs = *it;
    delete fs;
  }
  fileServers.clear();
}

