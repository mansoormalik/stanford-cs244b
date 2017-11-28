#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <iostream>
#include <list>
#include <vector>
#include <set>
#include <algorithm>
#include <string>
#include "appl.h"

using namespace std;

/* Base Constants */
#define TRUE 1
#define FALSE 0
#define WAIT_TIME_IN_SEC 1
#define WAIT_TIME_IN_MSEC 1000


/* NOTE: the protocol defines this as 5. But I observed the max
   retries being reached on the myth cluster when the machines became
   overloaded. It is possible for the NICs on a machine to overflow if
   the OS is not handling interrupts when packets come in. So bumping
   this to 10 for the purpose of the automated tests. */
#define MAX_ATTEMPTS 10

/* DEBUGGING */
#define DEBUG_PACKET_VALID 0

/* NOTE: turn on this flag to observe the packets been sent and received between
   a client and file server.
*/
#define DEBUG_DUMP_PACKETS 0

/* The multicast address is 224.1.1.1 */
#define MULTICAST_ADDR       0xe0010101

/* Protocol Constants */
#define MAGIC_NUMBER 0xdeadbeef
#define DRFS_VERSION 0x0001
#define BROADCAST_ID 0x00000000
#define INVALID_FD -1


typedef	struct sockaddr_in Sockaddr;


/* The packet and all the bodies of the different types of
   messages used in the DRFS protocol must be packed
*/
#pragma pack(push)
#pragma pack(1)
typedef	struct {
  uint32_t magicNumber;
  uint16_t version;
  uint8_t type;
  uint32_t sourceId;
  uint32_t destId;
  uint32_t sequenceNumber;
  uint8_t body[528];
} Packet;

typedef struct {
  uint8_t requestCode;
} JoinRequest;

typedef struct {
  char filename[128];
} OpenFileRequest;

typedef struct {
  uint32_t fileDesc;
} OpenFileResponse;

typedef struct {
  uint32_t blockId;
  uint32_t fileDesc;
  uint32_t byteOffset;
  uint32_t blockSize;
  char buffer[512];
} WriteBlk;

typedef struct {
  uint32_t transactionId;
  uint32_t fileDesc;
  uint32_t firstBlockId;
  uint32_t lastBlockId;
} PrepareToCommitRequest;

typedef struct {
  uint32_t transactionId;
  uint8_t responseCode;
  uint32_t numMissingBlocks;
  uint32_t missingBlockId[128];
} PrepareToCommitResponse;

typedef struct {
  uint32_t transactionId;
} CommitRequest;


typedef struct {
  uint32_t transactionId;
} CommitResponse;

typedef struct {
  uint32_t fileDesc;
} AbortRequest;

typedef struct {
  uint8_t responseCode;
} AbortResponse;

typedef struct {
  uint32_t fileDesc;
} CloseFileRequest;

typedef struct {
  uint8_t responseCode;
} CloseFileResponse;

#pragma pack(pop)

typedef	struct {
  short type;
  Packet packet;
  Sockaddr source;
} Event;


enum MessageType {
  JOIN_REQUEST,
  JOIN_RESPONSE,
  OPEN_FILE_REQUEST,
  OPEN_FILE_RESPONSE,
  WRITE_BLOCK,
  PREPARE_TO_COMMIT_REQUEST,
  PREPARE_TO_COMMIT_RESPONSE,
  COMMIT_REQUEST,
  COMMIT_RESPONSE,
  ABORT_REQUEST,
  ABORT_RESPONSE,
  CLOSE_FILE_REQUEST,
  CLOSE_FILE_RESPONSE,
};

enum JoinRequestCode {
  RESPOND_WITH_EXISTING_ID,
  RESPOND_WITH_NEW_ID
};

enum PrepareToCommitResponseCode {
  VOTE_YES,
  VOTE_NO_MISSING_BLOCKS,
  VOTE_NO_INVALID_FILE_DESC
};

enum AbortResponseCode {
  ABORT_SUCCESS,
  ABORT_FAILURE
};

enum CloseFileResponseCode {
  CLOSE_SUCCESS,
  CLOSE_FAILURE
};

enum EventType {
  TIMED_OUT,
  RECEIVED_PACKET,
};


class FileServer {
 public:
 FileServer(uint32_t id) : id_(id), isFileOpen_(false), 
    isValidLocalFileDesc_(false) {}
  inline unsigned int id() const { return id_; }
  void id(unsigned int id) { id_ = id; }
  inline Sockaddr* source()  { return &source_; }
  void source( Sockaddr *source) { memcpy(&source_, source, sizeof(Sockaddr)); }
  inline int localFileDesc() const { return localFileDesc_; }
  inline bool isFileOpen() const { return isFileOpen_; }
  void isFileOpen(bool ifo) { isFileOpen_ = ifo; }
  inline bool isValidLocalFileDesc() const { return isValidLocalFileDesc_; }
  void isValidLocalFileDesc(bool ivlfd) { isValidLocalFileDesc_ = ivlfd; }
  void localFileDesc(int fd) { localFileDesc_ = fd; }
  inline uint32_t remoteFileDesc() const { return remoteFileDesc_; }
  void remoteFileDesc(uint32_t fd) { remoteFileDesc_ = fd; }
  inline string filename() const { return filename_; }
  void filename(string fn) { filename_ = fn; }
  inline PrepareToCommitResponseCode prepareToCommitResponseCode() 
    const { return prepareToCommitResponseCode_; }
  void prepareToCommitResponseCode( PrepareToCommitResponseCode rc )
    { prepareToCommitResponseCode_ = rc; }
  inline AbortResponseCode abortResponseCode() const { return abortResponseCode_; }
  void abortResponseCode( AbortResponseCode rc )  { abortResponseCode_ = rc; }
  inline uint32_t transactionId() const { return transactionId_; }
  void transactionId(uint32_t tid) { transactionId_ = tid; }
 private:
  uint32_t id_;
  Sockaddr source_;
  bool isFileOpen_;
  bool isValidLocalFileDesc_;
  uint32_t localFileDesc_;
  uint32_t remoteFileDesc_;
  string filename_;
  PrepareToCommitResponseCode prepareToCommitResponseCode_;
  AbortResponseCode abortResponseCode_;
  uint32_t transactionId_;
};


class Client {
 public:
 Client(uint32_t id) : id_(id), isFileOpen_(false) {}
  inline bool isFileOpen() const { return isFileOpen_; }
  void isFileOpen( bool ifo ) { isFileOpen_ = ifo; }
  inline int fileDesc() const { return fileDesc_; }
  void fileDesc( int fd ) { fileDesc_ = fd; }
 private:
  unsigned int id_;
  bool isFileOpen_;
  int fileDesc_;
};

void SendJoinRequest(int numServers);
void RecvJoinRequest(Packet *incoming);
void SendOpenFileRequest(char *filename);
void RecvOpenFileRequest(Packet *incoming);
void SendOpenFileResponse(int filedesc, uint32_t destId);
void RecvOpenFileResponse(Packet *incoming);
void SendWriteBlock(WriteBlk *wb);
void RecvWriteBlock(WriteBlk *wb);
void SendPrepareToCommitRequest(uint32_t transactionId, uint32_t fileDesc,
                                uint32_t firstBlockId, uint32_t lastBlockId);
void RecvPrepareToCommitRequest(Packet *incoming);
void SendPrepareToCommitResponse(uint32_t transactionId, PrepareToCommitResponseCode rc,  
                                 uint32_t destinationId);
void RecvPrepareToCommitResponse(Packet *incoming);
void SendCommitRequest(uint32_t transactionId);
void RecvCommitRequest(Packet *incoming);
void SendCommitResponse(uint32_t transactionId, uint32_t destId);
void RecvCommitResponse(Packet *incoming);
void SendAbortRequest(uint32_t fileDesc);
void RecvAbortRequest(Packet *incoming);
void SendAbortResponse(AbortResponseCode rc, uint32_t destId);
void RecvAbortResponse(Packet *incoming);
void SendCloseFileRequest(uint32_t fileDesc);
void RecvCloseFileRequest(Packet *incoming);
void SendCloseFileResponse(CloseFileResponseCode rc, uint32_t destId);
void RecvCloseFileResponse(Packet *incoming);
void netInit(unsigned short port, int packetLoss);
void CreatePacketHeader(Packet *outgoing, unsigned int messageType, uint32_t destination);
void SendPacket(Packet *outgoing);
void Error(const char *msg);
bool IsDropPacket();
int GenerateId();
void dumpPacket(Packet *packet, int isSending);
bool IsDestAddrMine (Packet *packet);
bool IsPacketValid (Packet *packet);
long int TimeElapsedInMilliseconds(struct timeval *timestamp);
unsigned int NextSequenceNumber();
unsigned int NextFileDescriptor();
bool IsDuplicateId(unsigned int id, Sockaddr *source);
bool IsUnknownId(unsigned int id);
void ProcessPacketClient(Packet *incoming);
void ProcessPacketFileServer(Packet *incoming);
void NextEvent(Event *event, int sock);
bool IsDoneRecvOpenFileResponses();
bool IsDoneRecvPrepareToCommitResponses();
bool IsDoneRecvCommitResponses();
bool IsDoneRecvCloseFileResponses();
bool IsAllFileServersVoteYes();
bool IsAllFileServersCommited();
bool IsReceivedAllAbortResponses();
bool IsAbortSuccessful();
bool IsBlockMissing(uint32_t firstBlockId, uint32_t lastBlockId);
uint32_t FirstUncommitedBlockId();
uint32_t LastUncommitedBlockId();
uint32_t NextBlockId();
uint32_t NextTransactionId();
FileServer* LookupFileServer(uint32_t id);
WriteBlk* LookupUncommitedBlock(uint32_t blockId);
bool BlockCompare(WriteBlk* first, WriteBlk* second);
void ClearUncommitedBlocks();
void ClearFileServers();
void HandleServerResponses(MessageType msgType, 
                           int *numAttempts, 
                           bool (IsTerminate()));

#endif
