/*****************/
/* Mansoor Malik */
/* CS 244B	 */
/* Spring 2012	 */
/*****************/

#define DEBUG

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include <client.h>
#include "protocol.h"
#include "appl.h"

extern int sock;
extern Sockaddr groupAddr;

extern set<FileServer* > openFileResponses;
extern set<FileServer* > prepareToCommitResponses;
extern set<FileServer* > commitResponses;
extern set<FileServer* > abortResponses;
extern set<FileServer* > closeFileResponses;
extern set<FileServer *> fileServers;

extern list<WriteBlk* > uncommitedBlocks;
extern vector<uint32_t > missingBlocks;

extern uint32_t currentTransactionId;
extern uint32_t currentFileDesc;
extern Client* meClient;

int
InitReplFs( unsigned short portNum, int packetLoss, int numServers ) {

#ifdef DEBUG
  printf( "InitReplFs: Port number %d, packet loss %d percent, %d servers\n",
          portNum, packetLoss, numServers );
#endif

  netInit(portNum, packetLoss);
  SendJoinRequest(numServers);

  return( NormalReturn );  
}

int
OpenFile( char * filename ) {

  ASSERT( filename );

#ifdef DEBUG
  printf( "OpenFile: Opening File '%s'\n", filename );
#endif

  int numAttempts;

  /* check that the filename length does not exceed maximum value */
  if (strlen(filename) > MaxFileNameLength)
    return ErrorReturn;
  
  /* client can only open one file at a time */
  if (meClient->isFileOpen())
    return ErrorReturn;

  openFileResponses.clear();
  
  numAttempts = 0;
  while (numAttempts < MAX_ATTEMPTS) {
    numAttempts++; 
    SendOpenFileRequest(filename);
    HandleServerResponses(OPEN_FILE_RESPONSE,
                          &numAttempts,
                          IsDoneRecvOpenFileResponses);
  }

  if (IsDoneRecvOpenFileResponses()) {
    meClient->isFileOpen( true);
    meClient->fileDesc( currentFileDesc );
    return currentFileDesc;
  }
  return ErrorReturn;
}

int
WriteBlock( int fd, char * buffer, int byteOffset, int blockSize ) {
  ASSERT( fd >= 0 );
  ASSERT( byteOffset >= 0 );
  ASSERT( buffer );
  ASSERT( blockSize >= 0 && blockSize < MaxBlockLength );

#ifdef DEBUG
  printf( "WriteBlock: Writing FD=%d, Offset=%d, Length=%d\n",
          fd, byteOffset, blockSize );
#endif


  /* cannot write block if a file is not open */
  if (!(meClient->isFileOpen()))
    return ErrorReturn;

  /* cannot write block with an invalid fd */
  if (meClient->fileDesc() != fd)
    return ErrorReturn;

  /* cannot have more than 128 updates before commit */
  if (uncommitedBlocks.size() == MaxWrites) 
    return ErrorReturn;

  /* file size cannot exceed 1 MB */
  if (byteOffset + blockSize > (int)MaxFileLength)
    return ErrorReturn;
  

  WriteBlk * wb = new WriteBlk;
  wb->blockId = htonl(NextBlockId());
  wb->fileDesc = htonl(fd);
  wb->byteOffset = htonl(byteOffset);
  wb->blockSize = htonl(blockSize);
  memcpy(wb->buffer, buffer, blockSize);

  SendWriteBlock(wb);

  /* save uncommited blocks in case in case we need to resend later */
  uncommitedBlocks.push_back(wb);

  return (blockSize);
}

/* The commit function implements the two-phase commit protocol as
   defined in the DRFS protocol. In the first phase, the client
   sends a prepare to commit request. At this stage, a server can send a
   yes vote or a no vote. If the no vote is because of missing
   blocks, then the client will resend the blocks and start another
   round of polling. If any server does not respond or responds with a no vote 
   because of an internal error or invalid fd then the client will abort the 
   transaction. Otherwise, the client will ask all servers to commit.

 */
int
Commit( int fd ) {
  int numAttempts;

  ASSERT( fd >= 0 );

#ifdef DEBUG
  printf( "Commit: FD=%d\n", fd );
#endif


  /* check that the file descriptor is valid */
  if (meClient->fileDesc() != fd)
    return ErrorReturn;

  currentTransactionId = NextTransactionId();
  uint32_t firstBlockId = FirstUncommitedBlockId();
  uint32_t lastBlockId = LastUncommitedBlockId();

  /* first phase of two-phase commit protocol */
  prepareToCommitResponses.clear();
  numAttempts = 0;
  while (numAttempts < MAX_ATTEMPTS) {
    numAttempts++;
    SendPrepareToCommitRequest(currentTransactionId, 
                               fd, 
                               firstBlockId, 
                               lastBlockId);
    HandleServerResponses(PREPARE_TO_COMMIT_RESPONSE, 
                          &numAttempts, 
                          IsDoneRecvPrepareToCommitResponses);

    /* retransmit missing blocks requested by file servers */
    if (missingBlocks.size() > 0) {
      for (unsigned int i = 0; i < missingBlocks.size(); i++) {
	WriteBlk* wb = LookupUncommitedBlock( missingBlocks.at(i) );
	SendWriteBlock(wb);
      }
      missingBlocks.clear();
      /* restart after sending out missing blocks */
      numAttempts = 0;
    }
  }
  
  /* if all file servers did not vote yes than abort and return */
  if (!IsAllFileServersVoteYes()) {
    ClearUncommitedBlocks();
    Abort(fd);
    return (ErrorReturn);
  }

  /* second phase of two-phase commit protocol */
  commitResponses.clear();
  numAttempts = 0;
  while (numAttempts < MAX_ATTEMPTS) {
    numAttempts++;
    SendCommitRequest(currentTransactionId); 
    
    HandleServerResponses(COMMIT_RESPONSE,
                          &numAttempts,
                          IsDoneRecvCommitResponses);
    
  }

  /* free up resources used by uncommited blocks */
  ClearUncommitedBlocks();

  if (IsAllFileServersCommited())
    return NormalReturn;
  return ErrorReturn;

}

int
Abort( int fd )
{

  ASSERT( fd >= 0 );

#ifdef DEBUG
  printf( "Abort: FD=%d\n", fd );
#endif

  int numAttempts;


  
  /* return if a file is not open or the file descriptor is invalid */
  if (!(meClient->isFileOpen()) ||
      (meClient->fileDesc() != fd) )
    return ErrorReturn;
  
  /* if there were no updates then return immediately */
  if (uncommitedBlocks.size() == 0)
    return NormalReturn;


  ClearUncommitedBlocks();
  abortResponses.clear();

  numAttempts = 0;
  while (numAttempts < MAX_ATTEMPTS) {
    numAttempts++;
    SendAbortRequest(fd);
    HandleServerResponses(ABORT_RESPONSE, 
                          &numAttempts, 
                          IsReceivedAllAbortResponses);
  }
  
  if (IsAbortSuccessful())
    return NormalReturn;
  return ErrorReturn;

}

int
CloseFile( int fd ) {

  ASSERT( fd >= 0 );

#ifdef DEBUG
  printf( "Close: FD=%d\n", fd );
#endif


  int numAttempts;

  /* return if a file is not open or the file descriptor is invalid */
  if (!(meClient->isFileOpen()) ||
      (meClient->fileDesc() != fd) )
    return ErrorReturn;

  meClient->isFileOpen( false );

  /* per clarification on Piazza call commit only if there have been 
     intervening writes */
  if (uncommitedBlocks.size() > 0) {
    if (Commit( fd ) == ErrorReturn)
      return ErrorReturn;
  }

  /* CloseFile */
  closeFileResponses.clear();
  numAttempts = 0;
  while (numAttempts < MAX_ATTEMPTS) {
    numAttempts++;
    SendCloseFileRequest(fd);
    HandleServerResponses(CLOSE_FILE_RESPONSE,
                          &numAttempts,
                          IsDoneRecvCloseFileResponses);
  }
  
  if (!IsDoneRecvCloseFileResponses())
    return (ErrorReturn);

  return(NormalReturn);
}






