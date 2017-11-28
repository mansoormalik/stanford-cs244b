#include "main.h"
#include <string>
#include <list>
#include <queue>
#include "mazewar.h"

static bool updateView;	/* true if update needed */
MazewarInstance::Ptr M;

/* Use this socket address to send packets to the multi-cast group. */
static Sockaddr groupAddr;
#define MAX_OTHER_RATS  (MAX_RATS - 1)


/* static functions used in the toplevel module */
static void SendJoinGameRequest(char *ratName);
static void SendLeaveGameRequest();
static void SendLeaveGameResponse(PlayerId departing);
static void SendLocationStatus();
static void SendMissileStatus(Missile *missile);
static void SendTagConfirmationRequest(Missile *missile);
static void SendTagConfirmationResponse(PlayerId shooter, 
                                        MissileId missileId, 
                                        TagCode tagCode);
static void SendForceStopGame(PlayerId *unknown);
static void RecvJoinGameRequest(MW244BPacket *incoming);
static void RecvLeaveGameRequest(MW244BPacket *incoming);
static void RecvLeaveGameResponse(MW244BPacket *incoming);
static void RecvLocationStatus(MW244BPacket *incoming);
static void RecvMissileStatus(MW244BPacket *incoming);
static void RecvTagConfirmationRequest(MW244BPacket *incoming);
static void RecvTagConfirmationResponse(MW244BPacket *incoming);
static PlayerId GeneratePlayerId();
static SequenceNumber NextSequenceNumber();
static MissileId NextMissileId();
static RatIndexType* FindFreeRatIndex();
static Player* LookupActiveAdversary(PlayerId *playerId);
static Player* LookupLeavingAdversary(PlayerId *playerId);
static Player* RatIndexToPlayer(RatIndexType ratIndex); 
static bool IsRatInCell(Loc x, Loc y);
static bool IsPlayerRatInCell(Player *player, Loc x, Loc y);
static bool IsPacketValid(MW244BPacket *incoming);
static bool IsDestAddrMine(MW244BPacket *incoming);
static bool IsMissileHitPlayer(Missile *missile);
static bool IsMissileHitWall(Missile *missile);
static bool IsMissilePreviouslyCreated(PlayerId *shooter, 
                                       MissileId *missileId);
static bool IsAllowedToShoot();
static bool IsMessageFromUnknownPlayer(MW244BPacket *incoming);
static bool IsMessageFromLeavingPlayer(MW244BPacket *incoming);
static bool IsDropPacket();
static bool IsTagConfirmationExists(Missile *missile);
static bool IsDuplicatePlayerId(PlayerId *playerId);
static bool IsJoiningGameWithAnotherPlayer(PlayerId *playerId);
static void SendLocationStatusIfTimeout();
static void SendTagConfirmationIfTimeout();
static void RemovePlayerIfTimeout();
static void RemoveMissileIfTimeout();
static void DisplayMyMissile(Missile *missile);
static void HandleLeavingGame();

/* collections for tracking shared state of other players */
list<Player* > activeAdversaries;
Player* joiningAdversary;
list<Player* > leavingAdversaries;

/* collections for tracking state of missiles */
list<Missile *> flyingMissiles; 
list<Missile *> stoppedMissiles;

/* queue for tracking incoming and outgoing tags */
queue<TagConfirmation *> incomingTagConfirmations;
queue<TagConfirmation *> outgoingTagConfirmations;

/* used for tracking free indexes for new player rats */
queue<RatIndexType *> availableRatIndexes;



int main(int argc, char *argv[])
{

  // initialize the pseudo-random number generator
  srand((unsigned int)time(NULL));

  char *ratName;

  signal(SIGHUP, quit);
  signal(SIGINT, quit);
  signal(SIGTERM, quit);

  Loc x(0);
  Loc y(0);
  Direction direction(0);

  // a user can specify the rat name, x pos, y pos, and direction
  // on the command line
  bool isCmdLineParams;
  if (argc == 5) 
    isCmdLineParams = true;
  else 
    isCmdLineParams = false;
  
  if (isCmdLineParams) {
    ratName = argv[1];
    x = Loc( atoi( argv[2] ) );
    y = Loc( atoi( argv[3] ) );
    switch (*argv[4]) {
    case 'n' : direction = Direction(NORTH); break;
    case 's' : direction = Direction(SOUTH); break;
    case 'e' : direction = Direction(EAST); break;
    case 'w' : direction = Direction(WEST); break;
    }
  } else {
    getName("Welcome to CS244B MazeWar!\n\nYour Name", &ratName);
  }

  M = MazewarInstance::mazewarInstanceNew(string(ratName));
  strncpy(M->myName_, ratName, NAMESIZE);
  MazeInit(argc, argv);

  // do local initialization
  for (int i = 1; i < MAX_RATS; i++) {
    availableRatIndexes.push(new RatIndexType(i));
  }
  joiningAdversary = NULL;

  // we start the game if a JoinGameRequest was successful
  SendJoinGameRequest(ratName);

  // set our rat's position and direction
  if (isCmdLineParams) {
    M->xlocIs(x);
    M->ylocIs(y);
    M->dirIs(direction);
  } else {
    NewPosition(M);
    free(ratName);  
  }

  // start play loop
  play();

  return (0);
}


void
play(void)
{
  MWEvent event;
  MW244BPacket incoming;

  event.eventDetail = &incoming;

  while (TRUE) {
    NextEvent(&event, M->theSocket());
    if (!M->peeking())
      switch(event.eventType) {
      case EVENT_A:
        aboutFace();
        break;

      case EVENT_S:
        leftTurn();
        break;

      case EVENT_D:
        forward();
        break;

      case EVENT_F:
        rightTurn();
        break;

      case EVENT_BAR:
        backward();
        break;

      case EVENT_LEFT_D:
        peekLeft();
        break;

      case EVENT_MIDDLE_D:
        shoot();
        break;

      case EVENT_RIGHT_D:
        peekRight();
        break;

      case EVENT_NETWORK:
        processPacket(&event);
        break;

      case EVENT_INT:
        /* If no other players are in the game then go
           ahead and quit. If other players are present
           then send LeaveGameRequest messages per
           the Ratpack protocol. Do not allow the user
           to override the number and timing of messages.
         */
        if (activeAdversaries.size() > 0) {
          if (!M->isLeavingGame()) {
            M->isLeavingGame(true);
            struct timeval now;
            gettimeofday(&now, NULL);
            M->firstLeaveGameReq(now);
            M->numLeaveGameReq(1);
            SendLeaveGameRequest();
          }
        } else {
          quit(0);
        }
        break;
      }
    else
      switch (event.eventType) {
      case EVENT_RIGHT_U:
      case EVENT_LEFT_U:
        peekStop();
        break;

      case EVENT_NETWORK:
        processPacket(&event);
        break;
      }


    // if we are leaving game then do nothing except process
    // incoming packets and check if we have timed out
    if (M->isLeavingGame()) {
      HandleLeavingGame();
    } else {
      manageMissiles();
      updateView = true;
      DoViewUpdate();
      
      // these housekeeping tasks can be done approximately every 1000 ms
      // timeout interval for next event was changed to 200 ms in winsys.cpp
      // this was done to conform to the Ratpack protocol which requires that
      // MissileStatus updates be sent every 200 ms
      static int everyFifthTime = 0;
      if ( (everyFifthTime % 5) == 0) {
        SendLocationStatusIfTimeout();
        SendTagConfirmationIfTimeout();
        RemoveMissileIfTimeout();
        RemovePlayerIfTimeout();
      }
    }

  }
}

static void 
dumpPacket(MW244BPacket *packet, int isSending) {

  const char *header = "%s msgType: %s srcId: %u dstId: %u ";
  char pktDirection[][5] = { "RECV", "SEND" };
  char messageType[][32] = { "JoinGameRequest", "JoinGameResponse", "LeaveGameRequest",
                         "LeaveGameResponse", "LocationStatus", "MissileStatus",
                         "TagConfirmationRequest", "TagConfirmationResponse",
                         "ForceStopGame" };
  char buf[256];
  sprintf(buf, 
          header,
          pktDirection[isSending],
          messageType[packet->type],
          htonl(packet->sourcePlayerId), 
          htonl(packet->destPlayerId));
  printf("%s", buf);

  bzero(buf, sizeof(buf));
  switch(packet->type) {
  case JOIN_GAME_REQUEST: {
    const char *msg = "playerName: %s\n";
    sprintf(buf, msg, (const char *)packet->body);
    printf("%s", buf);
    break;
  }
  case JOIN_GAME_RESPONSE: {
    char joinGameResponseCode[][32] = { "Accept", "RejectMaxPlayerLimitReached",
                                      "RejectDuplicatePlayerID", "RetryAnotherJoinInProgress" };
    JoinGameResponse jgr;
    memcpy(&jgr, packet->body, sizeof(jgr));
    const char *msg = "code: %s playerName: %s totPlayers: %hd\n";
    sprintf(buf, msg, joinGameResponseCode[jgr.code], jgr.name, jgr.totalPlayers);
    printf("%s", buf);
    break;
  }
  case LEAVE_GAME_REQUEST: {
    printf("\n");
    break;
  }
  case LEAVE_GAME_RESPONSE: {
    printf("\n");
    break;
  }
  case LOCATION_STATUS: {
    LocationStatus ls;
    memcpy(&ls, packet->body, sizeof(ls));
    const char *msg = "x: %d y:%d dir: %hd score: %d\n";
    sprintf(buf, msg, ntohs(ls.xLoc), ntohs(ls.yLoc), ls.direction, ntohl(ls.score));
    printf("%s", buf);
    break;
  }
  case MISSILE_STATUS: {
    MissileStatus ms;
    memcpy(&ms, packet->body, sizeof(ms));
    const char *msg = "x: %d y:%d dir: %hd missileId: %d\n";
    sprintf(buf, msg, ntohs(ms.xLoc), ntohs(ms.yLoc), ms.direction, ntohl(ms.missileId));
    printf("%s", buf);
    break;
  }
  case TAG_CONFIRMATION_REQUEST: {
    TagConfirmationRequest tcr;
    memcpy(&(tcr), packet->body, sizeof(tcr));
    const char *msg = "missileId: %d\n";
    sprintf(buf, msg, ntohl(tcr.missileId));
    printf("%s", buf);
    break;
  }
  case TAG_CONFIRMATION_RESPONSE: {
    TagConfirmationResponse tcr;
    memcpy(&(tcr), packet->body, sizeof(tcr));
    const char *msg = "missileId:%d tagCode:%hd\n";
    sprintf(buf, msg, ntohl(tcr.missileId), tcr.tagCode);
    printf("%s", buf);
    break;
  }
  case FORCE_STOP_GAME: {
    printf("\n");
    break;
  }
  }
}

static void CreatePacketHeader(MW244BPacket *outgoing, 
                               MessageType messageType,
                               uint32_t destination) {
  outgoing->magicNumber = htonl(MAGIC_NUMBER);
  outgoing->version = htons(RATPACK_VERSION);
  outgoing->type = messageType;
  outgoing->sourcePlayerId = htonl(M->playerId().value());
  outgoing->destPlayerId = destination;
  outgoing->sequenceNumber = htonl(NextSequenceNumber().value());
}

static bool IsDropPacket() {
  // srand is invoked once in main
  static unsigned int const MAX_VALUE = 100;
  unsigned short randomValue = rand() % MAX_VALUE;
  if (randomValue > PERCENT_DROPPED_PACKETS) 
    return false;
  return true;
}

static void SendPacket(MW244BPacket *outgoing) {
  if (IsDropPacket()) { 
    // don't send packet; print diagnostics if needed
    if (DEBUG_DUMP_PACKETS) printf("DROP ");
  } else {
    if (sendto((int)M->theSocket(), outgoing, sizeof(MW244BPacket), 0,
               (struct sockaddr*) &groupAddr, sizeof(Sockaddr)) < 0)
      { MWError("error sending packet"); }
  }
  if (DEBUG_DUMP_PACKETS) dumpPacket(outgoing, 1);
}



/* We may receive multiple TagConfirmationRequest messages while a
   missile is in flight. We need to create one TagConfirmation
   object when the first message is received. This utility
   function is used to check if a TagConfirmation object
   was created previously.
 */
static bool IsTagConfirmationExists(Missile *missile) {
  for (unsigned int i = 0; i < incomingTagConfirmations.size(); i++) {
    TagConfirmation *tc = incomingTagConfirmations.front();
    incomingTagConfirmations.pop();
    incomingTagConfirmations.push(tc);
    Missile *m = tc->missile();
    if ( (m->shooter() == missile->shooter()) &&
         (m->missileId() == missile->missileId()) )
      return true;
  }
  return false;
}

static bool IsDuplicatePlayerId(PlayerId *playerId) {
  if (playerId->value() == M->playerId().value())
    return true;
  for (list<Player *>::iterator it = activeAdversaries.begin();
       it != activeAdversaries.end();
       it++) {
    Player *player = *it;
    if (playerId->value() == player->playerId().value())
      return true;
  }
  for (list<Player *>::iterator it = leavingAdversaries.begin();
       it != leavingAdversaries.end();
       it++) {
    Player *player = *it;
    if (playerId->value() == player->playerId().value())
      return true;
  }
  return false;
}

static bool IsJoiningGameWithAnotherPlayer(PlayerId *playerId) {
  if (joiningAdversary == NULL) return false;

  if (joiningAdversary->playerId() != *playerId)
    return true;

  return false;
}

/* This function is invoked when a player receives a LocationStatus
   update from an adversary. The function first checks to make sure
   that the sequence number is higher than the last sequence number
   for a LocationStatus. This function then verifies that another
   player's rat is not in the same location. The Ratpack protocol
   specifies that a LocationStatus update will be discarded if another
   rat already occupies the same position.  At this point, the shared state
   of games will diverge temporarily. They will reconverge when one of
   the rats moves to a cell location that is unoccupied.  
*/
static void RecvLocationStatus(MW244BPacket *incoming) {

  LocationStatus ls;
  memcpy(&ls, incoming->body, sizeof(ls));
  ls.xLoc = htons(ls.xLoc);
  ls.yLoc = htons(ls.yLoc);
  ls.score = htonl(ls.score);

  PlayerId playerId( ntohl(incoming->sourcePlayerId ));

  Player *player = LookupActiveAdversary(&playerId);
  if (player != NULL) {

    // discard LocationStatus if the sequence number is not higher
    uint32_t sequenceNumber = ntohl(incoming->sequenceNumber);
    if ( sequenceNumber < player->lastSeqNumLocation() ) {
      return;
    }
    // update the sequence number and the timestamp used for detecting timeout
    player->lastSeqNumLocation(sequenceNumber);
    player->locationStatusReceived();

    // perform update only if there is no rat in the cell or it is the 
    // players own rat (the score or direction may have changed)
    Loc x(ls.xLoc);
    Loc y(ls.yLoc);
    Direction direction(ls.direction);

    if (!IsRatInCell(x, y)) {
      SetRatPosition(player->ratIndex()->value(), x, y, direction);
      player->score( Score(ls.score) );
      UpdateScoreCard(player->ratId()->value());
    } else if (IsPlayerRatInCell(player, x, y)) {
      SetRatPosition(player->ratIndex()->value(), x, y, direction);
      player->score( Score(ls.score) );
      UpdateScoreCard(player->ratId()->value());
    }
  }
}

/* This function creates a missile upon receiving a MissileStatus
   message. The Ratpack protocol specifies that an adversary will send
   a MissileStatus message when first firing a missile and will then
   send updates at least every 200 ms. So this function ensures that a
   missile is only created once. Once a missile is created the missile
   location is updated in the local game. Subsequent MissileStatus
   messages for the same missile are ignored.  
*/
static void RecvMissileStatus(MW244BPacket *incoming) {

  MissileStatus ms;
  memcpy(&ms, incoming->body, sizeof(ms));
  ms.xLoc = htons(ms.xLoc);
  ms.yLoc = htons(ms.yLoc);
  ms.missileId = htonl(ms.missileId);

  PlayerId shooter( ntohl(incoming->sourcePlayerId) );
  MissileId missileId(ms.missileId);

  // discard MissileStatus if the sequence number is not higher
  Player *player = LookupActiveAdversary(&shooter);
  if (player != NULL) {
    uint32_t sequenceNumber = ntohl(incoming->sequenceNumber);
    if ( sequenceNumber < player->lastSeqNumMissile() ) {
      return;
    }
    player->lastSeqNumMissile(sequenceNumber);
  }


  // the RatPack protocol specifies that an adversary will send a 
  // MissileStatus message when a message is first launched and
  // every 200ms thereafterwards so only create a missile once
  if ( !(IsMissilePreviouslyCreated(&shooter, &missileId)) ) {
    Missile *missile = new Missile( ms.xLoc,
                                    ms.yLoc,
                                    ms.direction,
                                    ms.missileId,
                                    shooter.value() );
    flyingMissiles.push_back(missile);
  }
}


static void SendLeaveGameRequest() {
  MW244BPacket outgoing;
  CreatePacketHeader(&outgoing,
                     LEAVE_GAME_REQUEST,
                     htonl(PLAYER_ID_BROADCAST));
  SendPacket(&outgoing);
}


static void SendLeaveGameResponse(PlayerId departing) {
  MW244BPacket outgoing;
  CreatePacketHeader(&outgoing,
                     LEAVE_GAME_RESPONSE,
                     htonl(departing.value()));
  SendPacket(&outgoing);
}


static void 
SendLocationStatus() {
  MW244BPacket packet;
  CreatePacketHeader(&packet, 
                     LOCATION_STATUS, 
                     htonl(PLAYER_ID_BROADCAST));
  LocationStatus ls;
  ls.xLoc = htons(M->xloc().value());
  ls.yLoc = htons(M->yloc().value());
  ls.direction = M->dir().value();
  ls.score = htonl(M->score().value());
  memcpy(&(packet.body), &ls, sizeof(ls));
  SendPacket(&packet);
  struct timeval now;
  gettimeofday(&now, NULL);
  M->myLastLocUpdate(now);
}


static void
SendMissileStatus(Missile* missile) {
  MW244BPacket packet;
  CreatePacketHeader(&packet, 
                     MISSILE_STATUS, 
                     htonl(PLAYER_ID_BROADCAST));
  MissileStatus ms;
  ms.xLoc = htons(missile->curX().value());
  ms.yLoc = htons(missile->curY().value());
  ms.direction = missile->direction().value();
  ms.missileId = htonl(missile->missileId().value());
  memcpy(&(packet.body), &ms, sizeof(ms));
  SendPacket(&packet);
}

static void
SendTagConfirmationRequest(Missile *missile) {
  MW244BPacket packet;
  CreatePacketHeader(&packet, 
                     TAG_CONFIRMATION_REQUEST, 
                     htonl(missile->victim().value()));
  TagConfirmationRequest tcr;
  tcr.missileId = htonl(missile->missileId().value());
  memcpy(&(packet.body), &tcr, sizeof(tcr));
  SendPacket(&packet);
}


static void
SendTagConfirmationResponse(PlayerId shooter, MissileId missileId, TagCode tagCode) {
  MW244BPacket packet;
  CreatePacketHeader(&packet,
                     TAG_CONFIRMATION_RESPONSE,
                     htonl(shooter.value()));
  TagConfirmationResponse tcr;
  tcr.missileId = htonl(missileId.value());
  tcr.tagCode = tagCode;
  memcpy(&(packet.body), &tcr, sizeof(tcr));
  SendPacket(&packet);
}

static void SendForceStopGame(PlayerId *unknown) {
  MW244BPacket packet;
  CreatePacketHeader(&packet,
                     FORCE_STOP_GAME,
                     htonl(unknown->value()));
  SendPacket(&packet);
}

/* The Ratpack protocol specifies that a user may send up to three
   LeaveGameRequest messages. The rat of the player who is leaving
   should be removed from the maze when the first request is
   received. But we still need to keep the class that identifies a
   player in case subsequent messages are received. The 
   RemovePlayerIfTimeout function will eventually free up the
   resources allocated to the departing player once the timeout
   expires.
 */
static void
RecvLeaveGameRequest(MW244BPacket *incoming) {

  PlayerId playerId( ntohl(incoming->sourcePlayerId) );
  Player* player;

  if ((player = LookupLeavingAdversary(&playerId)) != NULL) {
    unsigned int MAX_LEAVE_GAME_REQUESTS = 3;
    unsigned int nlgr = player->numLeaveGameReq();
    if (nlgr < MAX_LEAVE_GAME_REQUESTS) {
      player->numLeaveGameReq( ++nlgr );
      SendLeaveGameResponse(playerId);
    }
  } else {
    for (list<Player *>::iterator it = activeAdversaries.begin();
         it != activeAdversaries.end();
         it++) {
      player = *it;
      if (player->playerId() == playerId) {
        it = activeAdversaries.erase(it);
        leavingAdversaries.push_back(player);
        struct timeval now;
        gettimeofday(&now, NULL);
        player->firstLeaveGameReq(now);
        player->numLeaveGameReq(1);
        SendLeaveGameResponse(playerId);
        ClearRatPosition ( *(player->ratIndex()) );
        ClearScoreLine ( *(player->ratIndex()) );
        availableRatIndexes.push( player->ratIndex() );
    }
    }
  }
}


/* This function isinvoked by a node that earlier sent a LeaveGameRequest.
   It removes each player from the activeAdversaries list when a
   LeaveGameResponse is received. If the number of activeAdversaries
   goes down to 0 then, the node goes ahead quits the application
   since all activeAdversaries have responded. The HandleLeavingGame
   function is responsible for implementing the Ratpack protocol and
   will ensure that at least 3 LeaveGameRequests are sent if responses
   are still missing from some nodes.
 */
static void
RecvLeaveGameResponse(MW244BPacket *incoming) {
  PlayerId playerId( ntohl(incoming->sourcePlayerId) );
  Player* player;
  for (list<Player *>::iterator it = activeAdversaries.begin();
       it != activeAdversaries.end();
       it++) {
    player = *it;
    if (player->playerId() == playerId) {
      it = activeAdversaries.erase(it);
      delete player;
    }
  }
  if (activeAdversaries.size() == 0) { 
    quit(0);
  }
}

/* This function is invoked when an adversary sends us
   a TagConfirmationRequest. This function needs to be
   idempotent because an adversary may send up to 3
   TagConfirmationRequest messages.

   When a request is receieved, a missile in our local 
   copy of the game may be in of the following three states:
   (a) still in flight
   (b) hit a player
   (c) hit a wall

   If the missile is still in flight then we delay sending
   a message until the missile has hit a player or wall. We
   create and add a TagConfirmation object to the 
   incomingTagConfirmations queue. We process this queue 
   later when a call is made to manageMissiles().

   If the missile has hit our player then we send a
   TagConfirmationResponse with an ACCEPT_TAG code. 

   If the missile has hit another player or a wall in our local
   copy of the game we send a TagConfirmationResponse with
   a REJECT_TAG code.

 */
static void
RecvTagConfirmationRequest(MW244BPacket* incoming) {
  
  PlayerId shooter( ntohl(incoming->sourcePlayerId) );

  TagConfirmationRequest tcr;
  memcpy(&tcr, incoming->body, sizeof(tcr));
  tcr.missileId = ntohl(tcr.missileId);

  // check if missile stopped and hit my rat
  for (list<Missile *>::iterator it = stoppedMissiles.begin();
       it != stoppedMissiles.end(); it++) {

    Missile *missile = *it;
    if (missile->shooter() == shooter &&
        missile->missileId().value() == tcr.missileId &&
        missile->victim() == M->playerId()) {
      unsigned int ntcr = missile->numTagConfResponse();
      if (ntcr == 3) return;
      if (ntcr == 0) {
        M->scoreIs( M->score().value() - 5 );
        UpdateScoreCard(M->myRatId().value());
        NewPosition(M);
      }
      SendTagConfirmationResponse(shooter, 
                                  missile->missileId(), 
                                  ACCEPT_TAG);
      missile->numTagConfResponse( ++ntcr );
      return;
    }    
  }

    // if the missile is still in flight then insert tag confirmation
    // into queue.
  for (list<Missile *>::iterator it = flyingMissiles.begin();
       it != flyingMissiles.end(); it++) {

    Missile *missile = *it;
    if (missile->shooter() == shooter &&
        missile->missileId().value() == tcr.missileId ) {
      if (!IsTagConfirmationExists(missile)) {
        TagConfirmation *tc = new TagConfirmation(missile);
        incomingTagConfirmations.push( tc );
      }    
      return;
    }
  }
  
  // if we got to this point then there was no hit in our local game
  SendTagConfirmationResponse( shooter,
                               MissileId( tcr.missileId ),
                               REJECT_TAG );
}

static void
RecvTagConfirmationResponse(MW244BPacket* incoming) {
  TagConfirmationResponse tcr;
  memcpy(&tcr, &(incoming->body), sizeof(tcr));
  tcr.missileId = ntohl(tcr.missileId);

  for(unsigned int i = 0; i < outgoingTagConfirmations.size(); i++) {
    
    TagConfirmation *tc = outgoingTagConfirmations.front();
    outgoingTagConfirmations.pop();
    
    if ( (tc->missile()->victim().value() == ntohl(incoming->sourcePlayerId)) &&
         (tc->missile()->missileId().value() == tcr.missileId) ) {
      if (tcr.tagCode == ACCEPT_TAG) {
        M->scoreIs( M->score().value() + 11 );
        UpdateScoreCard(M->myRatId().value());
      }
      delete tc;
      return;
    }
  }
}

static	Direction _aboutFace[NDIRECTION] = {SOUTH, NORTH, WEST, EAST};
static	Direction _leftTurn[NDIRECTION] = {WEST, EAST, NORTH, SOUTH};
static	Direction _rightTurn[NDIRECTION] = {EAST, WEST, SOUTH, NORTH};

void
aboutFace(void)
{
  M->dirIs(_aboutFace[MY_DIR]);
  updateView = TRUE;
  SendLocationStatus();
}

void
leftTurn(void)
{
  M->dirIs(_leftTurn[MY_DIR]);
  updateView = TRUE;
  SendLocationStatus();
}

void
rightTurn(void)
{
  M->dirIs(_rightTurn[MY_DIR]);
  updateView = TRUE;
  SendLocationStatus();
}

/* remember ... "North" is to the right ... positive X motion */

void
forward(void)
{
  register int	tx = MY_X_LOC;
  register int	ty = MY_Y_LOC;

  switch(MY_DIR) {
  case NORTH:	if (!M->maze_[tx+1][ty])	tx++; break;
  case SOUTH:	if (!M->maze_[tx-1][ty])	tx--; break;
  case EAST:	if (!M->maze_[tx][ty+1])	ty++; break;
  case WEST:	if (!M->maze_[tx][ty-1])	ty--; break;
  default:
    MWError("bad direction in Forward");
  }
  if (IsRatInCell(Loc(tx), Loc(ty))) return;
  if ((MY_X_LOC != tx) || (MY_Y_LOC != ty)) {
    M->xlocIs(Loc(tx));
    M->ylocIs(Loc(ty));
    updateView = TRUE;
  }
  SendLocationStatus();
}

void backward()
{
  register int	tx = MY_X_LOC;
  register int	ty = MY_Y_LOC;

  switch(MY_DIR) {
  case NORTH:	if (!M->maze_[tx-1][ty])	tx--; break;
  case SOUTH:	if (!M->maze_[tx+1][ty])	tx++; break;
  case EAST:	if (!M->maze_[tx][ty-1])	ty--; break;
  case WEST:	if (!M->maze_[tx][ty+1])	ty++; break;
  default:
    MWError("bad direction in Backward");
  }
  if (IsRatInCell(Loc(tx), Loc(ty))) return;
  if ((MY_X_LOC != tx) || (MY_Y_LOC != ty)) {
    M->xlocIs(Loc(tx));
    M->ylocIs(Loc(ty));
    updateView = TRUE;
  }
  SendLocationStatus();
}

void peekLeft()
{
  M->xPeekIs(MY_X_LOC);
  M->yPeekIs(MY_Y_LOC);
  M->dirPeekIs(MY_DIR);

  switch(MY_DIR) {
  case NORTH:	if (!M->maze_[MY_X_LOC+1][MY_Y_LOC]) {
      M->xPeekIs(MY_X_LOC + 1);
      M->dirPeekIs(WEST);
    }
    break;

  case SOUTH:	if (!M->maze_[MY_X_LOC-1][MY_Y_LOC]) {
      M->xPeekIs(MY_X_LOC - 1);
      M->dirPeekIs(EAST);
    }
    break;

  case EAST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC+1]) {
      M->yPeekIs(MY_Y_LOC + 1);
      M->dirPeekIs(NORTH);
    }
    break;

  case WEST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC-1]) {
      M->yPeekIs(MY_Y_LOC - 1);
      M->dirPeekIs(SOUTH);
    }
    break;

  default:
    MWError("bad direction in PeekLeft");
  }

  /* if any change, display the new view without moving! */

  if ((M->xPeek() != MY_X_LOC) || (M->yPeek() != MY_Y_LOC)) {
    M->peekingIs(TRUE);
    updateView = TRUE;
  }
}

void peekRight()
{
  M->xPeekIs(MY_X_LOC);
  M->yPeekIs(MY_Y_LOC);
  M->dirPeekIs(MY_DIR);

  switch(MY_DIR) {
  case NORTH:	if (!M->maze_[MY_X_LOC+1][MY_Y_LOC]) {
      M->xPeekIs(MY_X_LOC + 1);
      M->dirPeekIs(EAST);
    }
    break;

  case SOUTH:	if (!M->maze_[MY_X_LOC-1][MY_Y_LOC]) {
      M->xPeekIs(MY_X_LOC - 1);
      M->dirPeekIs(WEST);
    }
    break;

  case EAST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC+1]) {
      M->yPeekIs(MY_Y_LOC + 1);
      M->dirPeekIs(SOUTH);
    }
    break;

  case WEST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC-1]) {
      M->yPeekIs(MY_Y_LOC - 1);
      M->dirPeekIs(NORTH);
    }
    break;

  default:
    MWError("bad direction in PeekRight");
  }

  /* if any change, display the new view without moving! */

  if ((M->xPeek() != MY_X_LOC) || (M->yPeek() != MY_Y_LOC)) {
    M->peekingIs(TRUE);
    updateView = TRUE;
  }
}

void peekStop()
{
  M->peekingIs(FALSE);
  updateView = TRUE;
}

static bool IsMissilePreviouslyCreated(PlayerId *shooter, MissileId *missileId) {
  // check if missile is in list of stopped missiles
  for (list<Missile *>::iterator it = stoppedMissiles.begin();
       it != stoppedMissiles.end(); it++) {
    
    Missile *missile = *it;
    if ( (missile->shooter() == *shooter) &&
         (missile->missileId() == *missileId) ) {
      return true;
    }
  }    

  // check if missile is in list of flying missiles
  for (list<Missile *>::iterator it = flyingMissiles.begin();
       it != flyingMissiles.end(); it++) {

    Missile *missile = *it;
    if ( (missile->shooter() == *shooter) &&
         (missile->missileId() == *missileId) ) {
      return true;
    }
  }
  return false;
}

void shoot()
{
  if (!IsAllowedToShoot())  return;

  M->scoreIs( M->score().value()-1 );
  UpdateScoreCard(M->myRatId().value());

  MissileId missileId = NextMissileId();
  Missile *missile = new Missile( M->xloc().value(),
                                  M->yloc().value(),
                                  M->dir().value(),
                                  missileId.value(),
                                  M->playerId().value() );
  flyingMissiles.push_back(missile);
  SendMissileStatus(missile);
}

void quit(int sig)
{
  StopWindow();
  exit(0);
}


/* This function selects a new position for the local rat. 
   The position is selected at random but the position
   will not contain a wall or another rat.   
 */
void NewPosition(MazewarInstance::Ptr m)
{
  Loc newX(0);
  Loc newY(0);
  Direction dir(0); /* start on occupied square */

  while ( (M->maze_[newX.value()][newY.value()]) &&
          (!(IsRatInCell(newX, newY))) ) {
    /* MAZE[XY]MAX is a power of 2 */
    newX = Loc(random() & (MAZEXMAX - 1));
    newY = Loc(random() & (MAZEYMAX - 1));
  }

  /* prevent a blank wall at first glimpse */

  if (!m->maze_[(newX.value())+1][(newY.value())]) dir = Direction(NORTH);
  if (!m->maze_[(newX.value())-1][(newY.value())]) dir = Direction(SOUTH);
  if (!m->maze_[(newX.value())][(newY.value())+1]) dir = Direction(EAST);
  if (!m->maze_[(newX.value())][(newY.value())-1]) dir = Direction(WEST);

  m->xlocIs(newX);
  m->ylocIs(newY);
  m->dirIs(dir);
}

void MWError(const char *s)

{
  StopWindow();
  fprintf(stderr, "CS244BMazeWar: %s\n", s);
  perror("CS244BMazeWar");
  exit(-1);
}

Score GetRatScore(RatIndexType ratId)
{
  if (ratId.value() == 	M->myRatId().value())
    { return(M->score()); }
  else { 
    Player *adversary = RatIndexToPlayer(ratId);
    if (adversary != NULL)
      return adversary->score().value();
    else
      return (0); 
  }
}

const char *GetRatName(RatIndexType ratId)
{
  if (ratId.value() ==	M->myRatId().value())
    { return(M->myName_); }
  else { 
    Player *player = RatIndexToPlayer(ratId);
    if (player != NULL) 
      return player->name().c_str();
    else
      return "Dummy";
  }
}


/* This function examines each queued up TagConfirmationRequest and
   then compares it with each missile to see whether it hit our
   rat. The state of missiles is updated in the manageMissile
   function. If the missile is still in flight we keep the request in
   the queue and reexamine it later. If the missile hit our rat then
   we send a response with an ACCEPT_TAG and remove the request from
   the queue. If the missile did not hit our rat and is no longer in
   flight then we send a response with a REJECT_TAG and remove the
   request from the queue. The RatPack protocol specifies that a node
   may send up to 3 TagConfirmationRequests so we keep track of how many
   responses we have sent out. We deduce 5 points from our score when
   we accept the first tag. Subsequent tag requests will not cause us
   to lower our score again.
*/
static void
HandleIncomingTagConfirmations() {

  for(unsigned int i = 0; i < incomingTagConfirmations.size(); i++) {
    
    TagConfirmation *tc = incomingTagConfirmations.front();
    incomingTagConfirmations.pop();
    
    // check if missile stopped and hit my rat
    bool hitRat = false;
    for (list<Missile *>::iterator it = stoppedMissiles.begin();
         it != stoppedMissiles.end(); it++) {
      Missile *missile = *it;
      if (missile->shooter() == tc->missile()->shooter() &&
          missile->missileId() == tc->missile()->missileId() &&
          missile->victim() == M->playerId()) {
        hitRat = true;
	unsigned int ntcr = missile->numTagConfResponse();
	if (ntcr < 3) {
          if (ntcr == 0) {
            M->scoreIs( M->score().value() - 5 );
            UpdateScoreCard(M->myRatId().value());
            NewPosition(M);
          }
          SendTagConfirmationResponse(tc->missile()->shooter(), 
                                      tc->missile()->missileId(), 
                                      ACCEPT_TAG);
          missile->numTagConfResponse( ++ntcr );
        }
        delete tc;

      }    
    }
    if (hitRat) continue;
    
    // if the missile is still in flight then insert tag confirmation back
    // into queue.
    bool inFlight = false;
    for (list<Missile *>::iterator it = flyingMissiles.begin();
         it != flyingMissiles.end(); it++) {
      
      Missile *missile = *it;
      if (missile->shooter() == tc->missile()->shooter() &&
          missile->missileId() == tc->missile()->missileId()) {
        inFlight = true;
        incomingTagConfirmations.push(tc);
      }    
    }
    if (inFlight) continue;

    // if we got to this point then there was no hit in our local game
    SendTagConfirmationResponse( tc->missile()->shooter(),
                                 tc->missile()->missileId(),
                                 REJECT_TAG );
    delete tc;
  }
}



/* This functions is called from the play() loop. This function does two
   main things:
   (1) examines pending TagConfirmation messages and handles them. This is
       necessary since a player may have sent us a TagConfirmationRequest
       while the missile was still in flight when we received it.
   (2) advances missiles based on the rate of one cell every 200 ms; a
       missile has a firedAt timestamp so we use that timestamp to
       determine the position it should be at; we then advance it one cell
       at a time to see if it hits a rat or wall
 */

void manageMissiles()
{

  /* iterate through previous TagConfirmationRequests that were queued
     up and send a TagConfirmationResponse if my rat was tagged */
  if (incomingTagConfirmations.size() > 0) {
    HandleIncomingTagConfirmations();
  }

  for (list<Missile *>::iterator it = flyingMissiles.begin(); 
       it != flyingMissiles.end(); ) {

    Missile *missile = *it;

    // send MissileStatus update if needed
    const long int MISSILE_UPDATE_INTERVAL = 200;
    if (missile->shooter() == M->playerId()) {
      if (TimeElapsedInMilliseconds(missile->lastMessageSent()) > 
          MISSILE_UPDATE_INTERVAL) {
        SendMissileStatus(missile);
        struct timeval now;
        gettimeofday(&now, NULL);
        missile->lastMessageSent( now );
      }
    }

    
    // per the Ratpack protocol, missiles advance by one cell every 200 ms
    const unsigned short MISSILE_ONE_CELL_TIME = 200;
    
    // calculate how many cells the missile should have advanced by since
    // the time it was fired (where fired is the time of missile creation)
    long int timeElapsedInMs = TimeElapsedInMilliseconds( missile->firedAt() );
    short cellsAdvance = (int)(timeElapsedInMs / MISSILE_ONE_CELL_TIME);
    


    // this while loop will advance a missile one cell at a time
    // and after each move it will check if the missile hits a wall or player
    bool advancing = true;
    while (advancing) {
      if (IsMissileHitWall(missile)) { 
        advancing = false;
        missile->hitWall(true);
      } else if (IsMissileHitPlayer(missile)) {
        advancing = false;
        missile->hitPlayer(true);
      } else {
        switch(missile->direction().value()) {
        case NORTH:
          if (missile->curX().value() >= 
              missile->origX().value() + cellsAdvance) { 
            advancing = false;
          } else {
            missile->lastX( missile->curX() );
            missile->curX( missile->curX().value() + 1 );
          }
          break;
        case SOUTH:
          if (missile->curX().value() <= 
              missile->origX().value() - cellsAdvance) { 
            advancing = false;
          } else {
            missile->lastX( missile->curX() );
            missile->curX( missile->curX().value() - 1 );
          }
          break;
        case EAST:
          if (missile->curY().value() >= 
              missile->origY().value() + cellsAdvance) { 
            advancing = false;
          } else { 
            missile->lastY( missile->curY() );
            missile->curY( missile->curY().value() + 1 );
          }
          break;
        case WEST:
          if (missile->curY().value() <= 
              missile->origY().value() - cellsAdvance) { 
            advancing = false;
          } else {
            missile->lastY( missile->curY() );
            missile->curY( missile->curY().value() - 1 );
          }
          break;
        }

        if (missile->shooter() == M->playerId())
          DisplayMyMissile(missile);
 
      }
    }
    if (missile->hitWall()) {
      it = flyingMissiles.erase(it);
      stoppedMissiles.push_back(missile);
    } else if (missile->hitPlayer()) {
      stoppedMissiles.push_back(missile);
      if (missile->shooter() == M->playerId()) {
        SendTagConfirmationRequest(missile);
        TagConfirmation *tc = new TagConfirmation(missile);
        tc->timesMessageSent(1);
        outgoingTagConfirmations.push(tc);
      }
      it = flyingMissiles.erase(it);
    } else {
      it++;
    }
  }
}

/* This function return true if a missile hits a player or false
   otherwise. The function also sets the victim attribute in the
   missile to match the player that was hit.
 */
static bool
IsMissileHitPlayer(Missile *missile) {

  // check if missile tagged any players beside me
  for (list<Player *>::iterator it = activeAdversaries.begin();
       it != activeAdversaries.end();
       it++ ) {
    Player *player = *it;
    if (missile->shooter() == player->playerId())
      continue;
    RatIndexType *ratIndex = player->ratIndex();
    Rat rat = M->rat(*ratIndex);
    if ( missile->curX() == rat.x && 
         missile->curY() == rat.y ) {
      missile->victim(player->playerId());
      missile->hitPlayer(true);
      return true;
    } 
  }

  // check if another player's missile tagged our rat
  if (missile->shooter() != M->playerId()) {
    Rat rat = M->rat(RatIndexType(0));
    if ( missile->curX() == rat.x && 
         missile->curY() == rat.y ) {
      missile->victim(M->playerId());
      missile->hitPlayer(true);
      return true;
    }
  }
  return false;
}

static bool
IsMissileHitWall(Missile *missile) {
  short x = missile->curX().value();
  short y = missile->curY().value();
  if (M->maze_[x][y]) return true;
  return false;
}

/* This function returns false if our player has a missile
   in flight. Otherwise it return true.
 */

static bool IsAllowedToShoot() {
  for (list<Missile *>::iterator it = flyingMissiles.begin();
       it != flyingMissiles.end(); it++) {

    Missile *missile = *it;
    if (missile->shooter() == M->playerId())
      return false;
  }
  return true;
}


static void SendLocationStatusIfTimeout() {
  long int const maxIntervalInMilliseconds = 1000;
  struct timeval mllu = M->myLastLocUpdate();
  if (TimeElapsedInMilliseconds(&mllu) > maxIntervalInMilliseconds)
    SendLocationStatus();
}


/* The Ratpack protocol specifies that a player may send
   up to 3 TagConfirmationRequests before timing out.
   This function implements that capability and is called
   from the play loop.
 */
static void SendTagConfirmationIfTimeout() {
  static long int const TIMEOUT_FIRST_MESSAGE = 1000;
  static long int const TIMEOUT_SECOND_MESSAGE = 2000;
  static long int const TIMEOUT_THIRD_MESSAGE = 4000;

  for(unsigned int i = 0; i < outgoingTagConfirmations.size(); i++) {
    
    TagConfirmation *tc = outgoingTagConfirmations.front();
    outgoingTagConfirmations.pop();
        
    long int elapsed = TimeElapsedInMilliseconds(tc->timestamp());
    struct timeval now;
    
    if ( (tc->timesMessageSent() == 1) &&
         (elapsed > TIMEOUT_FIRST_MESSAGE) ) {
      SendTagConfirmationRequest(tc->missile());
      gettimeofday(&now, NULL);
      tc->timestamp(now);
      tc->timesMessageSent( tc->timesMessageSent() + 1 );
      outgoingTagConfirmations.push(tc);
    } else if ( (tc->timesMessageSent() == 2) &&
                (elapsed > TIMEOUT_SECOND_MESSAGE) ) {
      SendTagConfirmationRequest(tc->missile());
      gettimeofday(&now, NULL);
      tc->timestamp(now);
      tc->timesMessageSent( tc->timesMessageSent() + 1 );
      outgoingTagConfirmations.push(tc);
    } else if ( (tc->timesMessageSent() == 3) &&
                (elapsed > TIMEOUT_THIRD_MESSAGE) ) {
      delete tc;
    } else {
      outgoingTagConfirmations.push(tc);
    }
  }
}


/* The Ratpack protocol specifies that a player must send
   a LocationStatus message within 7 seconds of sending the first
   JoinGameRequest message.

   The Ratpack protocol specifies that an adversary is no longer
   considered alive if a LocationStatus update has not been
   received for over 10 seconds.

   The Ratpack protocol also specifies that a player that has sent
   a LeaveGameRequest message may send up to 2 additional messages.
   A player should be timed out 7 seconds after sending the first
   message.
 */
static void RemovePlayerIfTimeout() {

  // Remove player in joining state if timeout exceeded
  if (joiningAdversary != NULL) {
    struct timeval fjgr = joiningAdversary->firstJoinGameReq();
    long int elapsed = TimeElapsedInMilliseconds(&fjgr);
    long int MAX_TIMEOUT = 7000;
    if (elapsed > MAX_TIMEOUT) {
      delete joiningAdversary;
      joiningAdversary = NULL;
    }
  }
  
  // Remove players who have exceeded the maximum timeout
  // period for sending a heartbeat
  for (list<Player *>::iterator it = activeAdversaries.begin();
       it != activeAdversaries.end(); it++ ) {
    Player *adversary = *it;
    if (adversary->isLocationStatusTimeout()) {
      it = activeAdversaries.erase(it);
      ClearRatPosition(*(adversary->ratIndex()));
      ClearScoreLine ( *(adversary->ratIndex()) );
      availableRatIndexes.push( adversary->ratIndex() );
      delete adversary;
    }
  }

  // Remove players who had sent out a LeaveGameRequest
  // message and whose timeout period is expired
  for (list<Player *>::iterator it = leavingAdversaries.begin();
       it != leavingAdversaries.end(); 
       it++ ) {
    Player *adversary = *it;
    if (adversary->isLeaveGameTimeout()) {
      it = activeAdversaries.erase(it);
      // the rat was already cleared from the maze in the 
      // RecvLeaveGameRequest function
      delete adversary;
    }
  }


}


/* Missiles that hit a player or wall are removed after a timeout
   of 7 seconds. It is necessary to keep them around in the event
   that TagConfirmation requests or MissileStatus updates are 
   received.
 */

static void RemoveMissileIfTimeout() {
  long int TIMEOUT_AFTER_IMPACT = 7000;
  for (list<Missile *>::iterator it = stoppedMissiles.begin();
       it != stoppedMissiles.end(); 
       it++) {
    
    Missile *missile = *it;
    struct timeval toi = missile->timeOfImpact();
    long int elapsed = TimeElapsedInMilliseconds(&toi);

    // wait until the last TagConfirmation is removed as well
    if ( (elapsed > TIMEOUT_AFTER_IMPACT) &&
         (!IsTagConfirmationExists(missile)) )  {
      it = stoppedMissiles.erase(it);
      delete missile;
    }
  }
}

/*
  This function takes a missile as an argument and displays
  it on the bird's eye view of the maze. This function
  should only be called for our rat's missiles. The missiles
  of activeAdversaries are not shown in the bird's eye view.
 */

static void DisplayMyMissile(Missile *missile) {

  // don't show missile until it advances by one square
  // otherwise the missile will replace our rat
  if (missile->distanceFromOrigin() == 0) return;
  
  if ( (IsMissileHitWall(missile)) || 
       (IsMissileHitPlayer(missile)) ) {
    // check for corner case of rat standing next to wall
    // or another rat; do not erase in that case since
    // we would clear our own rat from the square
    if (missile->distanceFromOrigin() != 1) {
      clearSquare(missile->lastX(), missile->lastY());
    }
    return;
  };
  
  // don't erase when we are one square away from the origin
  // otherwise our rat will be erased
  bool  isErase;
  if (missile->distanceFromOrigin() == 1)
    isErase = false;
  else
    isErase = true;
  
  showMissile(missile->curX(), missile->curY(), missile->direction(),
              missile->lastX(), missile->lastY(), isErase);
}


/* This function implements the Ratpack protocol for leaving
   games. The initial SendLeaveGameRequest has already been
   sent so we may need to send two more requests. If all
   adversaries respond before then we will quit. This will be
   taken care of in the RecvLeaveGameResponse function.
*/
static void HandleLeavingGame() {
  static long int const TIMEOUT_FIRST_REQ = 1000;
  static long int const TIMEOUT_SECOND_REQ = 2000;
  
  struct timeval flgr = M->firstLeaveGameReq();
  long int elapsed = TimeElapsedInMilliseconds(&flgr);
  
  if ( (M->numLeaveGameReq() == 1) &&
       (elapsed > TIMEOUT_FIRST_REQ) ) {
    SendLeaveGameRequest();
    M->numLeaveGameReq( 2 );
  } else if ( (M->numLeaveGameReq() == 2) &&
              (elapsed > (TIMEOUT_FIRST_REQ + TIMEOUT_SECOND_REQ) ) ) {
    SendLeaveGameRequest();
    quit(0);
  }
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


void DoViewUpdate()
{
  if (updateView) {	/* paint the screen */
    ShowPosition(MY_X_LOC, MY_Y_LOC, MY_DIR);
    if (M->peeking())
      ShowView(M->xPeek(), M->yPeek(), M->dirPeek());
    else
      ShowView(MY_X_LOC, MY_Y_LOC, MY_DIR);
    updateView = FALSE;
  }
}


/* This function performs validation on incoming
   packets and invokes a handler based on the
   message type.
 */
void processPacket (MWEvent *event)
{
  MW244BPacket *incoming = event->eventDetail;

  // outgoing packets are randomly dropped instead of
  // incoming packets; refer to the SendPacket function

  if (!IsPacketValid(incoming)) return;
  if (DEBUG_DUMP_PACKETS) dumpPacket(incoming, 0); 


  /* If we are leaving game then ignore all messages
     except LeaveGameRespone messages addressed to us
     from players that are still active.
  */
  if (M->isLeavingGame()) {
    if ( (incoming->type == LEAVE_GAME_RESPONSE) && 
         (IsDestAddrMine(incoming)) &&
         (!IsMessageFromUnknownPlayer(incoming)) ) {
      RecvLeaveGameResponse(incoming);
    }
    return;
  }

  // a join game request will be from an unknown player 
  if (incoming->type == JOIN_GAME_REQUEST) {
    RecvJoinGameRequest(incoming);
    return;
  } 

  // a user who previously sent a join game request
  // only joins a game after sending their first
  // location status message
  
  if (joiningAdversary != NULL) {
    PlayerId playerId( ntohl(incoming->sourcePlayerId) );
    if ( (playerId == joiningAdversary->playerId()) &&
         (incoming->type == LOCATION_STATUS) ) {
      activeAdversaries.push_back(joiningAdversary);
      UpdateScoreCard( *(joiningAdversary->ratIndex()) );
      joiningAdversary = NULL;
    }
  }

  // If we receive any other type of message from an 
  // unknown player then force them to stop the game
  if ( (IsMessageFromUnknownPlayer(incoming)) &&
       (!IsMessageFromLeavingPlayer(incoming)) &&
       (incoming->type != FORCE_STOP_GAME) ) {
    PlayerId unknown( ntohl(incoming->sourcePlayerId) );
    SendForceStopGame(&unknown);
    return;
  }

  if (incoming->type == LEAVE_GAME_REQUEST) {
    RecvLeaveGameRequest(incoming);
  } else if (incoming->type == LOCATION_STATUS) {
    RecvLocationStatus(incoming);
  } else if (incoming->type == MISSILE_STATUS) {
    RecvMissileStatus(incoming);
  } else if (incoming->type == TAG_CONFIRMATION_REQUEST) {
    // only process requests with my destination address
    if (IsDestAddrMine(incoming))
      RecvTagConfirmationRequest(incoming);
  } else if (incoming->type == TAG_CONFIRMATION_RESPONSE) {
    // only process responses with my destination address
    if (IsDestAddrMine(incoming))
      RecvTagConfirmationResponse(incoming);
  } else if (incoming->type == FORCE_STOP_GAME) {
    if (IsDestAddrMine(incoming))
      quit(0);
  }
 
}

/* This function implements the Ratpack protocol for nodes
   that are trying to join a game. We need to perform a
   variety of checks to make sure that the maximum
   number of players has not been reached, that the
   new player is not using a duplicate player id, and that
   another join is not in progress. If these conditions
   hold then we accept the request.
*/

static void 
RecvJoinGameRequest(MW244BPacket *request) {

  /* Send response */
  MW244BPacket response;
  CreatePacketHeader(&response, 
                     JOIN_GAME_RESPONSE,
                     request->sourcePlayerId);

  PlayerId playerId( ntohl(request->sourcePlayerId) );

  JoinGameResponseCode responseCode;
  if ( ( activeAdversaries.size() + 1 ) == MAX_RATS ) { 
    responseCode = REJECT_MAX_PLAYER_LIMIT_REACHED;
  } else if ( IsDuplicatePlayerId(&playerId) ) {
    responseCode = RETRY_DUPLICATE_PLAYER_ID;
  } else if ( IsJoiningGameWithAnotherPlayer(&playerId) ) {
    responseCode = RETRY_ANOTHER_JOIN_IN_PROGRESS;
  } else {
    responseCode = ACCEPT;
  }

  JoinGameResponse jgr;
  jgr.code = responseCode;
  strcpy(jgr.name, M->myName_);
  jgr.totalPlayers = activeAdversaries.size() + 1;
  memcpy(&(response.body), &jgr, sizeof(jgr));
  if (DEBUG_DUMP_PACKETS) dumpPacket(&response, 1);
  if (sendto((int)M->theSocket(), &response, sizeof(response), 0,
             (struct sockaddr*) &groupAddr, sizeof(Sockaddr)) < 0)
    { MWError("Error sending packet in JoinGame"); }


  // don't create a player if we did not accept the request
  if (responseCode != ACCEPT) return;

  // don't create a player if this is the 2nd or 3rd JoinGameRequest
  // message from a player
  if ( joiningAdversary != NULL) {
    if (joiningAdversary->playerId() == playerId)
      return;
  }

  /* Create new player */
  char name[32];
  strncpy(name, (char *)request->body, sizeof(name));
  RatIndexType *ratIndex = FindFreeRatIndex();
  if (ratIndex == NULL) { 
    MWError("error getting free index for rat\n");
    exit(0);
  }
  Player *player = new Player ( ntohl(request->sourcePlayerId),
                        name,
                        ratIndex,
                        0 );
  
  joiningAdversary = player;
}


/* This function generates a random number from 1 to
   RAND_MAX. The value of RAND_MAX is platform specific.
   On a 32-bit Linux platform, this value is 2^31.
 */
static PlayerId GeneratePlayerId() {
  while(true) {
    // srand is invoked once in main
    PlayerId id(rand());
    if (id.value() != 0) 
      return id;
  }
}

static SequenceNumber NextSequenceNumber() {
  static SequenceNumber sequenceNumber(0);
  sequenceNumber = sequenceNumber.value() + 1;
  return sequenceNumber;
}

static MissileId NextMissileId() {
  static MissileId missileId(-1);
  missileId = missileId.value() + 1;
  return missileId;
}


/* Utility function that is used when new players join. The
   function keeps track of free rat indexes and returns the
   smallest one. The smallest one is used as the display 
   manager writes score lines by index number. Otherwise,
   we would end up with blank lines between player scores.
 */
static RatIndexType* FindFreeRatIndex() {
  if (availableRatIndexes.size() == 0) return NULL;
  RatIndexType *smallest = availableRatIndexes.front();
  availableRatIndexes.pop();
  for (unsigned int i = 0; i < availableRatIndexes.size(); i++) {
    RatIndexType *index = availableRatIndexes.front();
    availableRatIndexes.pop();
    if (smallest->value() < index->value()) {
      availableRatIndexes.push(index);
    } else {
      availableRatIndexes.push(smallest);
      smallest = index;
    }
  }
  return smallest;
}

static Player* LookupActiveAdversary(PlayerId *playerId) {
  for (list<Player *>::iterator it = activeAdversaries.begin();
       it != activeAdversaries.end();
       it++) {
    Player *player = *it;
    if (player->playerId() == *playerId)
      return player;
  }
  return NULL;
}

static Player* LookupLeavingAdversary(PlayerId *playerId) {
  for (list<Player *>::iterator it = leavingAdversaries.begin();
       it != leavingAdversaries.end();
       it++) {
    Player *player = *it;
    if (player->playerId() == *playerId)
      return player;
  }
  return NULL;
}

static bool IsMessageFromUnknownPlayer(MW244BPacket *incoming) {
  PlayerId playerId( ntohl(incoming->sourcePlayerId) );
  if (LookupActiveAdversary(&playerId) == NULL) 
    return true;
  return false;
}

static bool IsMessageFromLeavingPlayer(MW244BPacket *incoming) {
  PlayerId playerId( ntohl(incoming->sourcePlayerId) );
  if (LookupLeavingAdversary(&playerId) == NULL)
    return false;
  return true;
}


static Player* RatIndexToPlayer(RatIndexType ratIndex) {
  for (list<Player *>::iterator it = activeAdversaries.begin(); 
       it != activeAdversaries.end();  
       it++) {
    Player *player = *it;
    if (player->ratIndex()->value() == ratIndex.value())
      return player;
  }
  return NULL;
}


/*
  The location x and y specify a cell position in a maze. This
  function returns true if a rat is in this cell and the rat
  is in a plyaing state. It return false otherwise.
*/
static bool IsRatInCell(Loc x, Loc y) {
  for (int i = 0; i < MAX_RATS; i++) {
    RatIndexType index(i);
    Rat rat = M->rat(index);
    if (!rat.playing) continue;
    if ((rat.x == x) &&
        (rat.y == y)) 
      return true;
  } 
  return false;
}

static bool IsPlayerRatInCell(Player *player, Loc x, Loc y) {
  for (int i = 0; i < MAX_RATS; i++) {
    RatIndexType index(i);
    Rat rat = M->rat(index);
    if (!rat.playing) continue;
    if ( (rat.x == x) &&
         (rat.y == y) &&
         (index.value() == player->ratIndex()->value()) ) {
      return true;
    }
  } 
  return false;
}

static bool IsUnkownPlayer(PlayerId *playerId) {
 for (list<Player *>::iterator it = activeAdversaries.begin(); 
       it != activeAdversaries.end();  
       it++) {
    Player *player = *it;
    if (player->playerId() == *playerId)
      return false;
  }
 return true;
}

/* This function examines a packet and returns false if a any of
   the following conditions are met:
   (a) the packet was sent out by our own player
   (b) the magic number is invalid
   (c) the ratpack version number is invalid
   (d) the message type is invalid
 */
static bool 
IsPacketValid (MW244BPacket *packet) {
  if (ntohl(packet->sourcePlayerId) == M->playerId().value()) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: sent out by me\n");
    return false;
  }

  if (ntohl(packet->magicNumber) != MAGIC_NUMBER) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect magic number\n");
    return false;
  }

  if (ntohs(packet->version) != RATPACK_VERSION) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect ratpack version\n");
    return false;
  }

  if ( (packet->type < JOIN_GAME_REQUEST) ||
       (packet->type > FORCE_STOP_GAME) ) {
    if (DEBUG_PACKET_VALID) 
      printf("invalid packet: incorrect message type\n");
    return false;
  }

  return true;
}


static bool 
IsDestAddrMine (MW244BPacket *packet) {
  if (ntohl(packet->destPlayerId) == M->playerId().value()) 
    return true;
  return false;
}

/* This function implements the Ratpack protocol for sending a 
   JoinGameRequest message. The request should be re-transmitted 
   if no response is received. The maximum number of retry attempts is 2.
   The node should wait for 1 second in its first attempt, 2 seconds in
   its second attempt, and 4 seconds in its third attempt.

   If no response is received then the node should assume that no other
   player is currently on the network and should wait for another player to join.

   This function should also be ready to accept a JoinGameResponse message
   where the maximum number of players has been reached or the player has
   entered a duplicate name.

   This function should also be ready to accept a JoinGameResponse message
   where the node is asked to RetryAgain. This can occur if multiple nodes
   are trying to join at the same time.
 */
static void
SendJoinGameRequest(char *ratName) {
  int numRequests = 0;
  int numResponsesExpected = 0;
  int numRetryJoinInProgress = 0;
  bool isRetryAnotherJoinInProgress = false;
  bool isRetryDuplicatePlayerId = false;


  /* generate a random player id */
  M->playerId( PlayerId(GeneratePlayerId()) );

  while(numRequests < 3) {

    /* follow the Ratpack protocol for RetryAnotherJoinInProgress */
    if (isRetryAnotherJoinInProgress == true) {
      /* protocol specifies 3 attempts so there are 2 retries */
      int MAX_JOIN_IN_PROGRESS_RETRIES = 2;
      if (numRetryJoinInProgress == MAX_JOIN_IN_PROGRESS_RETRIES)
        MWError("ERROR: maximum retries with AnotherJoinInProgress\n");
      isRetryAnotherJoinInProgress = false;
      numRetryJoinInProgress++;
      numRequests = 0; //still get to send 3 requests if we get Accept
      long int randomDelayInMicroseconds =
        (rand() % (100000 * numRetryJoinInProgress));
      usleep(randomDelayInMicroseconds);
    }
    
    /* follow the Ratpack protocol for RetryDuplicatePlayerId */
    if (isRetryDuplicatePlayerId) {
      isRetryDuplicatePlayerId = false;
      numRequests = 0;
      M->playerId( PlayerId(GeneratePlayerId()) );
    }
              

    MW244BPacket packet;
    CreatePacketHeader(&packet,
                       JOIN_GAME_REQUEST,
                       htonl(PLAYER_ID_BROADCAST));


    if (strlen(ratName) > 31) {
      MWError("ratname cannot be longer than 31 characters"); 
    }
    strcpy((char *)&(packet.body), ratName);

    if (DEBUG_DUMP_PACKETS) dumpPacket(&packet, 1);
    if (sendto((int)M->theSocket(), &packet, sizeof(packet), 0,
               (struct sockaddr*) &groupAddr, sizeof(Sockaddr)) < 0)
      { MWError("Error sending packet in JoinGame"); }
    numRequests++;

    /* used for select call */
    fd_set fdmask;
    struct timeval timeout;
    int retval;
    timeout.tv_sec = pow(2, numRequests);
    timeout.tv_usec = 0;

    FD_ZERO(&fdmask);
    FD_SET((int)M->theSocket(), &fdmask);
    
    /* used for explictly keeping track of timing */
    struct timeval timeBeforeLoop;
    gettimeofday(&timeBeforeLoop, NULL);
    long int maxTimeInLoopInMilliseconds = (timeout.tv_sec) * 1000; 

    bool timedOut = false;
    while (!timedOut) {
      retval = select((int)M->theSocket()+1, &fdmask, NULL, NULL, &timeout);
      if (retval == 0) {
        timedOut = true;
      } else if (retval > 0) {

        /* we may never be timed out if the socket keeps reading location 
           or missile status updates from other nodes so check timing 
        */
        long int elapsed = TimeElapsedInMilliseconds(&timeBeforeLoop);
        if (elapsed > maxTimeInLoopInMilliseconds) {
          timedOut = true;
          continue;
        }

        /* got a packet on this socket but need to check that it is for us */
        MW244BPacket incoming;
        Sockaddr source;
        socklen_t sourceLen = sizeof(source);
        retval = recvfrom((int)M->theSocket(), (char *)&incoming,
                          sizeof(MW244BPacket), 0,
                          (struct sockaddr *)&source,
                          &sourceLen);

        if (retval <= 0) {
          printf("error receiving packet in JoinGame\n"); 
        } else {         

          if ( (IsPacketValid(&incoming)) &&
               (IsDestAddrMine(&incoming)) &&
               (incoming.type == JOIN_GAME_RESPONSE) ) {

            if (DEBUG_DUMP_PACKETS) dumpPacket(&incoming, 0);
            
            JoinGameResponse jgr;
            memcpy(&jgr, &(incoming.body), sizeof(jgr));
            numResponsesExpected = jgr.totalPlayers;

	    if (jgr.code == REJECT_MAX_PLAYER_LIMIT_REACHED) {
              MWError("Cannot join: maximum player limit reached\n");
	      quit(0);
	    } else if (jgr.code == RETRY_DUPLICATE_PLAYER_ID) {
              isRetryDuplicatePlayerId = true;
              continue;
            } else if (jgr.code == RETRY_ANOTHER_JOIN_IN_PROGRESS) {
              isRetryAnotherJoinInProgress = true;
              continue;
            }

            PlayerId playerId( ntohl(incoming.sourcePlayerId) );
            if (IsUnkownPlayer(&playerId)) {
              RatIndexType *ratIndex = FindFreeRatIndex();
              if (ratIndex == NULL) { 
                MWError("error getting free index for rat\n");
                exit(0);
              }
              Player *player = new Player( playerId.value(),
                                           jgr.name,
                                           ratIndex,
                                           0 );
              activeAdversaries.push_back(player);
            }
            /* we're done if the number of players created equals the 
               total number of players currently playing game */
            if ( activeAdversaries.size() == jgr.totalPlayers ) {
              return;
            }
          }
        }
      }
    }
  }
  // we'll get to this point if either the total number of responses did not match
  // the total number of players or we are the first one to join
  if (numResponsesExpected > 0) 
    MWError("ERROR: number of JoinGameResponse messages did not match number of players\n");
}



/* This will presumably be modified by you.
   It is here to provide an example of how to open a UDP port.
   You might choose to use a different strategy
*/
void
netInit()
{
  Sockaddr nullAddr;
  Sockaddr *thisHost;
  char buf[128];
  int reuse;
  u_char ttl;
  struct ip_mreq  mreq;

  /* MAZEPORT will be assigned by the TA to each team */
  M->mazePortIs(htons(MAZEPORT));

  gethostname(buf, sizeof(buf));
  if ((thisHost = resolveHost(buf)) == (Sockaddr *) NULL)
    MWError("who am I?");
  bcopy((caddr_t) thisHost, (caddr_t) (M->myAddr()), sizeof(Sockaddr));

  M->theSocketIs(socket(AF_INET, SOCK_DGRAM, 0));
  if (M->theSocket() < 0)
    MWError("can't get socket");

  /* SO_REUSEADDR allows more than one binding to the same
     socket - you cannot have more than one player on one
     machine without this */
  reuse = 1;
  if (setsockopt(M->theSocket(), SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) < 0) {
    MWError("setsockopt failed (SO_REUSEADDR)");
  }

  nullAddr.sin_family = AF_INET;
  nullAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  nullAddr.sin_port = M->mazePort();
  if (bind(M->theSocket(), (struct sockaddr *)&nullAddr,
           sizeof(nullAddr)) < 0)
    MWError("netInit binding");

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
  if (setsockopt(M->theSocket(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                 sizeof(ttl)) < 0) {
    MWError("setsockopt failed (IP_MULTICAST_TTL)");
  }

  /* uncomment the following if you do not want to receive messages that
     you sent out - of course, you cannot have multiple players on the
     same machine if you do this */
#if 0
  {
    u_char loop = 0;
    if (setsockopt(M->theSocket(), IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                   sizeof(loop)) < 0) {
      MWError("setsockopt failed (IP_MULTICAST_LOOP)");
    }
  }
#endif

  /* join the multicast group */
  mreq.imr_multiaddr.s_addr = htonl(MAZEGROUP);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(M->theSocket(), IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)
                 &mreq, sizeof(mreq)) < 0) {
    MWError("setsockopt failed (IP_ADD_MEMBERSHIP)");
  }

  /*
   * Now we can try to find a game to join; if none, start one.
   */

  printf("\n");

  /* set up some stuff strictly for this local sample */
  M->myRatIdIs(0);
  M->scoreIs(0);
  SetMyRatIndexType(0);

  /* Get the multi-cast address ready to use in SendData()
     calls. */
  memcpy(&groupAddr, &nullAddr, sizeof(Sockaddr));
  groupAddr.sin_addr.s_addr = htonl(MAZEGROUP);

}


