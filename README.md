# stanford-cs244b
This repository contains the two programming projects that I completed when I took CS244B. This was the core distributed systems course at Stanford.

These projects required:
1. the design of protocols such as those found in IETF RFCs
2. the implementation of protocols using C/C++

The designs and implementations were evaluated for:
1. synchronization of distributed state to ensure consistency
2. performance
3. recovery in the presence of failures

Project 1: Mazewar
--------------------
The first project was Mazewar, a distributed multiplayer game, where each player controls a rat in a maze. A player receives points for tagging other rats with a projectile and loses points for being tagged. The game is based on the X Windows version of Mazewar, which is in turn based on the game of Mazewar that ran on the Xerox Alto in the 1970s. 

The key aspect of this project was to design a protocol that specified:
1. the packet types and formats as found in IETF RFCs
2. the sequencing and semantics of the packets including how users locate, join, and leave games in mazewar
3. the timing of protocol events and how they provide for sufficient consistency for the game state

The design and specification were graded for:
1. correctness: the design had to support a distributed game that conformed to the game specifications
2. clarity and simplicty: the design must be clear and simple enough to ensure that it could be implemented correctly
3. performance: the design had to minimize network traffic and extraneous processing load

The key files in this directory are:
1. the writeup is contained in protocol.pdf and answers.pdf
2. the implementation of the protocol is in toplevel.cpp
3. the key data structures are defined in mazewar.h

The directory contains a number of other starter files that were provided as part of the project and were not modified by me.

Project 2: Replicated File System
---------------------------------
The second project was to design and implement a replicated file system. The key aspect of this file system was to optimize write and read performance by designing a protocol such that:
1. the client communicated with multiple file servers using multicast messaging (to minimize the number of control messages that were sent from 1 client to N servers)
2. data was transferred from the client to the servers using multicast (to minimize network traffic)
3. a 2-phase commit protocol had to be used to ensure consistency
4. due to academic time constraints, only the write path had to be implemented

The key files in this directory are:
1. the writeup is found in report.pdf
2. the protocol is defined in protocol.{h,c}
3. the client implementation is in client.{h,c}
4. the server implementation is in replFsServer.{h,c}

