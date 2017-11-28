/* $Header: mazewar.h,v 1.7 88/08/25 09:59:51 kent Exp $ */

/*
 * mazewar.h - Definitions for MazeWar
 *
 * Author:	Christopher A. Kent
 * 		Western Research Laboratory
 * 		Digital Equipment Corporation
 * Date:	Wed Sep 24 1986
 */

/* Modified by Michael Greenwald for CS244B, Mar 1992,
   Greenwald@cs.stanford.edu */

/* Modified by Nicholas Dovidio for CS244B, Mar 2009,
 * ndovidio@stanford.edu
 * This version now uses the CS249a/b style of C++ coding.
 */

/***********************************************************
Copyright 1986 by Digital Equipment Corporation, Maynard, Massachusetts,

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the names of Digital not be
used in advertising or publicity pertaining to disstribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifndef MAZEWAR_H
#define MAZEWAR_H


#include "fwk/NamedInterface.h"

#include "Nominal.h"
#include "Exception.h"
#include <string>

/* fundamental constants */

#ifndef	TRUE
#define	TRUE		1
#define	FALSE		0
#endif	/* TRUE */

/* You can modify this if you want to */
#define	MAX_RATS	8

/* A unique MAZEPORT will be assigned to your team by the TA */
#define	MAZEPORT	5016

/* The multicast group for Mazewar is 224.1.1.1 */
#define MAZEGROUP       0xe0010101
#define	MAZESERVICE	"mazewar244B"

/* The next two >must< be a power of two, because we subtract 1 from them
   to get a bitmask for random()
*/
#define	MAZEXMAX	32
#define	MAZEYMAX	16
#define	VECTORSIZE	55
#define	NAMESIZE	20
#define	NDIRECTION	4
#define	NORTH		0
#define	SOUTH		1
#define	EAST		2
#define	WEST		3
#define	NVIEW		4
#define	LEFT		0
#define	RIGHT		1
#define	REAR		2
#define	FRONT		3

/* Constants */
#define MAGIC_NUMBER 0xdeadbeef
#define RATPACK_VERSION 0x0001
#define PLAYER_ID_BROADCAST 0x00000000

/* DEBUGGING */
#define DEBUG_JOIN_GAME 0
#define DEBUG_PACKET_VALID 0
#define DEBUG_DUMP_PACKETS 1

#define PERCENT_DROPPED_PACKETS 0

/* types */

typedef	struct sockaddr_in Sockaddr;
typedef bool MazeRow[MAZEYMAX];
typedef	MazeRow MazeType [MAZEXMAX];
typedef	MazeRow	*MazeTypePtr;
//typedef	short						Direction;
typedef	struct {short	x, y; }		XYpoint;
typedef	struct {XYpoint	p1, p2;}	XYpair;
typedef	struct {short	xcor, ycor;}XY;
typedef	struct {unsigned short	bits[16];}	BitCell;
typedef	char RatName[NAMESIZE];




class Direction : public Ordinal<Direction, short> {
 public:
 Direction(short num) : Ordinal<Direction, short>(num) {
    if(num<NORTH || num>NDIRECTION){
      throw RangeException("Error: Unexpected value.\n");
    }
  }
};

class Loc : public Ordinal<Loc, short> {
 public:
 Loc(short num) : Ordinal<Loc, short>(num) {
    if(num<0){
      throw RangeException("Error: Unexpected negative value.\n");
    }
  }
};

class Score : public Ordinal<Score, int> {
 public:
 Score(int num) : Ordinal<Score, int>(num) {}
};


class RatIndexType : public Ordinal<RatIndexType, int> {
 public:
 RatIndexType(int num) : Ordinal<RatIndexType, int>(num) {
    if(num<0){
      throw RangeException("Error: Unexpected negative value.\n");
    }
  }
};

class RatId : public Ordinal<RatId, unsigned short> {
 public:
 RatId(unsigned short num) : Ordinal<RatId, unsigned short>(num) {
  }
};

class TokenId : public Ordinal<TokenId, long> {
 public:
 TokenId(long num) : Ordinal<TokenId, long>(num) {}
};


class PlayerId : public Ordinal<PlayerId, uint32_t>{
 public: 
 PlayerId(uint32_t id) : Ordinal<PlayerId, uint32_t>(id) {}
};

class SequenceNumber : public Ordinal<SequenceNumber, uint32_t>{
 public:
 SequenceNumber(uint32_t sn) : Ordinal<SequenceNumber, uint32_t>(sn) {}
};

class MissileId : public Ordinal<MissileId, uint32_t>{
 public: 
 MissileId(uint32_t id) : Ordinal<MissileId, uint32_t>(id) {}
};



class RatAppearance{

 public:
 RatAppearance() :  x(1), y(1), tokenId(0) {};
  bool visible;
  Loc x, y;
  short	distance;
  TokenId tokenId;
};

class Rat{

 public:
 Rat() :  playing(0), x(1), y(1), dir(NORTH){};
  bool playing;
  Loc	x, y;
  Direction dir;
};

typedef	RatAppearance RatApp_type [MAX_RATS];
typedef	RatAppearance *RatLook;

/* defined in display.c */
extern RatApp_type Rats2Display;

/* variables "exported" by the mazewar "module" */
class MazewarInstance :  public Fwk::NamedInterface  {
 public:
  typedef Fwk::Ptr<MazewarInstance const> PtrConst;
  typedef Fwk::Ptr<MazewarInstance> Ptr;

  static MazewarInstance::Ptr mazewarInstanceNew(string s){
    MazewarInstance * m = new MazewarInstance(s);
    return m;
  }

  inline Direction dir() const { return dir_; }
  void dirIs(Direction dir) { this->dir_ = dir; }
  inline Direction dirPeek() const { return dirPeek_; }
  void dirPeekIs(Direction dirPeek) { this->dirPeek_ = dirPeek; }

  inline long mazePort() const { return mazePort_; }
  void mazePortIs(long  mazePort) { this->mazePort_ = mazePort; }
  inline Sockaddr* myAddr() const { return myAddr_; }
  void myAddrIs(Sockaddr *myAddr) { this->myAddr_ = myAddr; }
  inline RatId myRatId() const { return myRatId_; }
  void myRatIdIs(RatId myRatId) { this->myRatId_ = myRatId; }

  inline bool peeking() const { return peeking_; }
  void peekingIs(bool peeking) { this->peeking_ = peeking; }
  inline int theSocket() const { return theSocket_; }
  void theSocketIs(int theSocket) { this->theSocket_ = theSocket; }
  inline Score score() const { return score_; }
  void scoreIs(Score score) { this->score_ = score; }
  inline Loc xloc() const { return xloc_; }
  void xlocIs(Loc xloc) { this->xloc_ = xloc; }
  inline Loc yloc() const { return yloc_; }
  void ylocIs(Loc yloc) { this->yloc_ = yloc; }
  inline Loc xPeek() const { return xPeek_; }
  void xPeekIs(Loc xPeek) { this->xPeek_ = xPeek; }
  inline Loc yPeek() const { return yPeek_; }
  void yPeekIs(Loc yPeek) { this->yPeek_ = yPeek; }
  inline int active() const { return active_; }
  void activeIs(int active) { this->active_ = active; }
  inline Rat rat(RatIndexType num) const { return mazeRats_[num.value()]; }
  void ratIs(Rat rat, RatIndexType num) { this->mazeRats_[num.value()] = rat; }
  inline PlayerId playerId() const { return playerId_; }
  void playerId(PlayerId pid) { playerId_ = pid; }
  inline bool isLeavingGame() const { return isLeavingGame_; }
  void isLeavingGame(bool ilg) { isLeavingGame_ = ilg; }
  inline struct timeval firstLeaveGameReq() const { return firstLeaveGameReq_; }
  void firstLeaveGameReq(struct timeval flgr) { firstLeaveGameReq_ = flgr; }
  inline unsigned int numLeaveGameReq() const { return numLeaveGameReq_; }
  void numLeaveGameReq(unsigned int nlgr) { numLeaveGameReq_ = nlgr; }
  inline struct timeval myLastLocUpdate() const { return myLastLocUpdate_; }
  void myLastLocUpdate(struct timeval mllu) { myLastLocUpdate_ = mllu; }

  MazeType maze_;
  RatName myName_;
 protected:
 MazewarInstance(string s) : Fwk::NamedInterface(s), dir_(0), dirPeek_(0), 
    myRatId_(0), score_(0), xloc_(1), yloc_(3), xPeek_(0), yPeek_(0), 
    isLeavingGame_(false), numLeaveGameReq_(0), playerId_(0) 
    {
      myAddr_ = (Sockaddr*)malloc(sizeof(Sockaddr));
      if(!myAddr_) {
        printf("Error allocating sockaddr variable");
      }
      gettimeofday(&myLastLocUpdate_, NULL);
    }
  Direction	dir_;
  Direction dirPeek_;

  long mazePort_;
  Sockaddr *myAddr_;
  Rat mazeRats_[MAX_RATS];
  RatId myRatId_;

  bool peeking_;
  int theSocket_;
  Score score_;
  Loc xloc_;
  Loc yloc_;
  Loc xPeek_;
  Loc yPeek_;
  int active_;
  bool isLeavingGame_;
  struct timeval firstLeaveGameReq_; 
  unsigned int numLeaveGameReq_;
  struct timeval myLastLocUpdate_;
  PlayerId playerId_;
};
extern MazewarInstance::Ptr M;

#define MY_RAT_INDEX		0
#define MY_DIR			M->dir().value()
#define MY_X_LOC		M->xloc().value()
#define MY_Y_LOC		M->yloc().value()

/* events */

#define	EVENT_A		1		/* user pressed "A" */
#define	EVENT_S		2		/* user pressed "S" */
#define	EVENT_F		3		/* user pressed "F" */
#define	EVENT_D		4		/* user pressed "D" */
#define	EVENT_BAR	5		/* user pressed space bar */
#define	EVENT_LEFT_D	6		/* user pressed left mouse button */
#define	EVENT_RIGHT_D	7		/* user pressed right button */
#define	EVENT_MIDDLE_D	8		/* user pressed middle button */
#define	EVENT_LEFT_U	9		/* user released l.M.b */
#define	EVENT_RIGHT_U	10		/* user released r.M.b */

#define	EVENT_NETWORK	16		/* incoming network packet */
#define	EVENT_INT	17		/* user pressed interrupt key */
#define	EVENT_TIMEOUT	18		/* nothing happened! */

extern unsigned short ratBits[];

/* The MW244BPacket and all the bodies of the different types of
   messages used in the Ratpack protocol must be packed
*/
#pragma pack(push)
#pragma pack(1)
typedef	struct {
  uint32_t magicNumber;
  uint16_t version;
  uint8_t type;
  uint32_t sourcePlayerId;
  uint32_t destPlayerId;
  uint32_t sequenceNumber;
  uint8_t body[64];
} MW244BPacket;

typedef struct {
  uint8_t code;
  char name[32];
  uint8_t totalPlayers;
} JoinGameResponse;

typedef struct {
  uint16_t xLoc;
  uint16_t yLoc;
  uint8_t direction;
  uint32_t score;
} LocationStatus;


typedef struct {
  uint16_t xLoc;
  uint16_t yLoc;
  uint8_t direction;
  uint32_t missileId;
} MissileStatus;


typedef struct {
  uint32_t missileId;
} TagConfirmationRequest;

typedef struct {
  uint32_t missileId;
  uint8_t tagCode;
} TagConfirmationResponse;

#pragma pack(pop)

typedef	struct {
  short eventType;
  MW244BPacket *eventDetail;
  Sockaddr eventSource;
} MWEvent;


enum JoinGameResponseCode {
  ACCEPT,
  REJECT_MAX_PLAYER_LIMIT_REACHED,
  RETRY_DUPLICATE_PLAYER_ID,
  RETRY_ANOTHER_JOIN_IN_PROGRESS
};

enum TagCode {
  ACCEPT_TAG,
  REJECT_TAG
};


/* defined in toplevel.cpp
   need to declare it here as it is used by classes such as Player 
*/
long int TimeElapsedInMilliseconds(struct timeval *timestamp);



/* The Player class ecapsulates the attributes and methods needed
   to manage the state of a player in a Mazewar game.
 */
class Player {
 public:
  Player( uint32_t playerId,
          char *name,
          RatIndexType *ratIndex,
          int score ) :
  playerId_(playerId),
  name_(name),
    score_(score), 
    numLeaveGameReq_(0),
    lastSeqNumLocation_(0),
    lastSeqNumMissile_(0)
  { 
    this->ratIndex_ = ratIndex;
    this->ratId_ = new RatId(ratIndex->value());
    gettimeofday(&firstJoinGameReq_, NULL);
    gettimeofday(&lastLocationStatus_, NULL);
  };
  ~Player() { delete ratId_; }

  inline PlayerId playerId() const { return playerId_; }
  inline string name() const { return name_; }
  inline Score score() const { return score_; }
  void score(Score score) { score_ = score; }
  inline RatIndexType* ratIndex() const { return ratIndex_; }
  inline RatId *ratId() const { return ratId_; }
  void locationStatusReceived() { gettimeofday(&lastLocationStatus_, NULL);
  }
  inline struct timeval firstJoinGameReq() const { return firstJoinGameReq_; }
  void firstJoinGameReq(struct timeval flgr) { firstJoinGameReq_ = flgr; }
  inline struct timeval firstLeaveGameReq() const { return firstLeaveGameReq_; }
  void firstLeaveGameReq(struct timeval flgr) { firstLeaveGameReq_ = flgr; }
  inline unsigned int numLeaveGameReq() const { return numLeaveGameReq_; }
  void numLeaveGameReq(unsigned int nlgr) { numLeaveGameReq_ = nlgr; }
  inline uint32_t lastSeqNumLocation() const { return lastSeqNumLocation_; }
  void lastSeqNumLocation(uint32_t nlgr) { lastSeqNumLocation_ = nlgr; }
  inline uint32_t lastSeqNumMissile() const { return lastSeqNumMissile_; }
  void lastSeqNumMissile(uint32_t nlgr) { lastSeqNumMissile_ = nlgr; }
  bool isLocationStatusTimeout() {
      long int const maxIntervalInMilliseconds = 10000;
      if (TimeElapsedInMilliseconds(&lastLocationStatus_) <
          maxIntervalInMilliseconds)
        return false;
      return true;
  }
  bool isLeaveGameTimeout() {
      long int const maxIntervalInMilliseconds = 7000;
      if (TimeElapsedInMilliseconds(&firstLeaveGameReq_) <
          maxIntervalInMilliseconds)
        return false;
      return true;
  }

 private:
  PlayerId playerId_;
  string name_;
  Score score_;
  RatIndexType *ratIndex_;
  RatId *ratId_;
  struct timeval lastLocationStatus_;
  struct timeval firstJoinGameReq_;
  struct timeval firstLeaveGameReq_;
  unsigned int numLeaveGameReq_;
  uint32_t lastSeqNumLocation_;
  uint32_t lastSeqNumMissile_;
};


/* The Missile class encapsulates the attributes and methods
   needed to manage missiles in a Mazewar game.
 */
class Missile {
 public:
  Missile ( uint16_t origX,
            uint16_t origY,
            uint8_t direction,
            uint32_t missileId,
            uint32_t shooter ) : 
    origX_(origX),
    origY_(origY),
    lastX_(origX),
    lastY_(origY),
    curX_(origX),
    curY_(origY),
    direction_(direction),
    missileId_(missileId),
    shooter_(shooter),
    hitWall_(false),
    hitPlayer_(false),
    victim_(0),
    numTagConfResponse_(0)
      { firedAt_ = new struct timeval;
        lastMessageSent_ = new struct timeval;
        gettimeofday(firedAt_, NULL); 
        gettimeofday(lastMessageSent_, NULL);
      };

  inline struct timeval* firedAt() const { return firedAt_; }
  inline struct timeval* lastMessageSent() { return lastMessageSent_; } 
  void lastMessageSent(struct timeval tv) { *lastMessageSent_ = tv; }
  inline unsigned int numTagConfResponse() const { return numTagConfResponse_; }
  void numTagConfResponse(unsigned int nlgr) { numTagConfResponse_ = nlgr; }
  inline Loc origX() const { return origX_; };
  inline Loc origY() const { return origY_; };
  inline Loc lastX() const { return lastX_; };
  void lastX(Loc x) { lastX_ = x; };
  inline Loc lastY() const { return lastY_; };
  void lastY(Loc y) { lastY_ = y; };
  inline Loc curX() const { return curX_; };
  void curX(Loc x) { curX_ = x; };
  inline Loc curY() const { return curY_; };
  void curY(Loc y) { curY_ = y; };
  inline Direction direction() const { return direction_; }
  inline MissileId missileId() const { return missileId_; }
  inline PlayerId shooter() const { return shooter_; }
  inline bool hitWall() const { return hitWall_; }
  void hitWall(bool hw) { 
    hitWall_ = hw;
    if (hitWall_) gettimeofday(&timeOfImpact_, NULL);
  }
  inline bool hitPlayer() const { return hitPlayer_; }
  void hitPlayer(bool hp) { 
    hitPlayer_ = hp;
    if (hitPlayer_) gettimeofday(&timeOfImpact_, NULL);
  }
  inline struct timeval timeOfImpact() const { return timeOfImpact_; }
  inline PlayerId victim() const { return victim_; }
  void victim(PlayerId v) { victim_ = v; }
  inline int distanceFromOrigin() { 
    return ( abs(curX_.value() - origX_.value()) + 
             abs(curY_.value() - origY_.value()) ); 
  }
  

 private:
  struct timeval *firedAt_;
  struct timeval *lastMessageSent_;
  Loc origX_;
  Loc origY_;
  Loc lastX_;
  Loc lastY_;
  Loc curX_;
  Loc curY_;
  Direction direction_;
  MissileId missileId_;
  PlayerId shooter_;

  bool hitWall_;
  bool hitPlayer_;
  PlayerId victim_;
  struct timeval timeOfImpact_;
  unsigned int numTagConfResponse_;
};


/* The TagConfirmation class is used for managing the
   state of TagConfirmationRequests and TagConfirmationResponses
   that are sent between a shooter and victim.
 */
class TagConfirmation {
 public:
 TagConfirmation( Missile* missile ) :
  missile_(missile),
    timesMessageSent_(0)
      { 
        timestamp_ = new struct timeval;
        gettimeofday(timestamp_, NULL); 
      };
  
  inline Missile* missile() const { return missile_; }
  inline struct timeval* timestamp() const { return timestamp_; }
  void timestamp(struct timeval tv) { *timestamp_ = tv; }
  inline unsigned int timesMessageSent() const { return timesMessageSent_; }
  void timesMessageSent(unsigned int ns) { timesMessageSent_ = ns; }

 private:
  Missile *missile_;
  struct timeval *timestamp_;
  unsigned int timesMessageSent_;
};

/* These messages are defined in the Ratpack protocol. */
enum MessageType {
  JOIN_GAME_REQUEST,
  JOIN_GAME_RESPONSE,
  LEAVE_GAME_REQUEST,
  LEAVE_GAME_RESPONSE,
  LOCATION_STATUS,
  MISSILE_STATUS,
  TAG_CONFIRMATION_REQUEST,
  TAG_CONFIRMATION_RESPONSE,
  FORCE_STOP_GAME
};

typedef struct {
  Loc X;
  Loc Y;
  Direction direction;
  Score score;
} JoinGameRequest;


void *malloc();
Sockaddr *resolveHost();

/* display.c */
void InitDisplay(int, char **);
void StartDisplay(void);
void ShowView(Loc, Loc, Direction);
void SetMyRatIndexType(RatIndexType);
void SetRatPosition(RatIndexType, Loc, Loc, Direction);
void ClearRatPosition(RatIndexType);
void ShowPosition(Loc, Loc, Direction);
void ShowAllPositions(void);
void showMe(Loc, Loc, Direction);
void clearPosition(RatIndexType, Loc, Loc);
void clearSquare(Loc xClear, Loc yClear);
void NewScoreCard(void);
void UpdateScoreCard(RatIndexType);
void FlipBitmaps(void);
void bitFlip(BitCell *, int size);
void SwapBitmaps(void);
void byteSwap(BitCell *, int size);
void showMissile(Loc x_loc, Loc y_loc, Direction dir, Loc prev_x, Loc prev_y, bool clear);


/* init.c */
void MazeInit(int, char **);
void ratStates(void);
void getMaze(void);
void setRandom(void);
void getName(const char *, char **);
void getString(const char *, char **);
void getHostName(char *, char **, Sockaddr *);
Sockaddr *resolveHost(char *);
bool emptyAhead();
bool emptyRight();
bool emptyLeft();
bool emptyBehind();

/* toplevel.c */
void play(void);
void aboutFace(void);
void leftTurn(void);
void rightTurn(void);
void forward(void);
void backward(void);
void peekLeft(void);
void peekRight(void);
void peekStop(void);
void shoot(void);
void quit(int);
void NewPosition(MazewarInstance::Ptr M);
void MWError(const char *);
Score GetRatScore(RatIndexType);
const char  *GetRatName(RatIndexType);
void manageMissiles(void);
void DoViewUpdate(void);
void processPacket(MWEvent *);
void netInit(void);



/* winsys.c */
void InitWindow(int, char **);
void StartWindow(int, int);
void ClearView(void);
void DrawViewLine(int, int, int, int);
void NextEvent(MWEvent *, int);
bool KBEventPending(void);
void HourGlassCursor(void);
void RatCursor(void);
void DeadRatCursor(void);
void HackMazeBitmap(Loc, Loc, BitCell *);
void DisplayRatBitmap(int, int, int, int, int, int);
void WriteScoreString(RatIndexType);
void ClearScoreLine(RatIndexType);
void InvertScoreLine(RatIndexType);
void NotifyPlayer(void);
void StopWindow(void);

#endif
