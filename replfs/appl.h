#ifndef APPL_H
#define APPL_H

/*****************/
/* Mansoor Malik */
/* CS 244B	 */
/* Spring 2012	 */
/*****************/

unsigned int const MaxWrites = 128;
unsigned int const MaxBlockLength = 512;
unsigned int const MaxFileNameLength = 127;
unsigned int const MaxFileLength = (1024) * (1024); /* 1MB */

/* A unique port number will be assigned to you by your TA */
int const ReplFsPort = 44022;

int const NormalExit = 0;
int const  ErrorExit = -1;


#endif

