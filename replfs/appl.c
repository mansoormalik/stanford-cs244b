/*****************/
/* Mansoor Malik */
/* CS 244B	 */
/* Spring 2012	 */
/*****************/

#define DEBUG

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <client.h>
#include <appl.h>

static void appl1(void);
static void appl5(void);
static void appl8(void);
static void appl10(void);
static void appl11(void);
static void appl14(void);
static void appl16(void);
static void appl17(void);
static void appl18(void);

int 
main(int argc, char **argv) {

  int fd;

  if( InitReplFs( ReplFsPort, 0, 1 ) < 0 ) {
    fprintf( stderr, "Error initializing the system\n" );
    return( ErrorExit );
  }
  
  appl18();
  return 0;

  
  printf ("%d\n", argc);


  int loopCnt;
  int byteOffset= 0;
  char strData[MaxBlockLength];

  char fileName[32] = "writeTest.txt";

  

  /*****************************/
  /* Open the file for writing */
  /*****************************/

  fd = OpenFile( fileName );
  if ( fd < 0 ) {
    fprintf( stderr, "Error opening file '%s'\n", fileName );
    return( ErrorExit );
  }

  /**************************************/
  /* Write incrementing numbers to the file */
  /**************************************/

  for ( loopCnt=0; loopCnt<128; loopCnt++ ) {
    sprintf( strData, "%d\n", loopCnt );

#ifdef DEBUG
    printf( "%d: Writing '%s' to file.\n", loopCnt, strData );
#endif

    if ( WriteBlock( fd, strData, byteOffset, strlen( strData ) ) < 0 ) {
      printf( "Error writing to file %s [LoopCnt=%d]\n", fileName, loopCnt );
      return( ErrorExit );
    }
    byteOffset += strlen( strData );
    
  }


  /**********************************************/
  /* Can we commit the writes to the server(s)? */
  /**********************************************/
  /*
  if ( Commit( fd ) < 0 ) {
    printf( "Could not commit changes to File '%s'\n", fileName );
    return( ErrorExit );
  }
  */


  /*
  if ( Abort( fd ) < 0 ) {
    printf( "Could not abort '%s'\n", fileName );
    return( ErrorExit );
  }
  */


  /**************************************/
  /* Close the writes to the server(s) */
  /**************************************/

  if ( CloseFile( fd ) < 0 ) {
    printf( "Error Closing File '%s'\n", fileName );
    return( ErrorExit );
  }


  printf( "Writes to file '%s' complete.\n", fileName );

  return( NormalExit );
}

/* ------------------------------------------------------------------ */

static int
openFile(char *file)
{
  int fd = OpenFile(file);
  if (fd < 0) printf("OpenFile(%s): failed (%d)\n", file, fd);
  return fd;
}

static int
commit(int fd)
{
  int result = Commit(fd);
  if (result < 0) printf("Commit(%d): failed (%d)\n", fd, result);
  return fd;
}

static int
closeFile(int fd)
{
  int result = CloseFile(fd);
  if (result < 0) printf("CloseFile(%d): failed (%d)\n", fd, result);
  return fd;
}

static void
appl1() {
  // simple case - just commit a single write update.                                                                                                                                        

  int fd;
  int retVal;

  fd = openFile( "file1" );
  retVal = WriteBlock( fd, "abcdefghijkl", 0, 12 );
  retVal = commit( fd );
  retVal = closeFile( fd );
}


static void
appl5() {

  // checks simple overwrite case                                                                                                                                                            

  int fd;
  int retVal;

  fd = openFile( "file5" );
  retVal = WriteBlock( fd, "aecdefghijkl", 0, 12 );
  retVal = WriteBlock( fd, "b", 1, 1 );
  retVal = commit( fd );
  retVal = closeFile( fd );
}

void appl8(void) {
  int fd;
  int retVal;
  int i;
  char commitStrBuf[512];

  for( i = 0; i < 512; i++ )
    commitStrBuf[i] = '1';

  fd = openFile( "file8" );

  // write first transaction starting at offset 512                                                                                                                                          
  for (i = 0; i < 50; i++)
    retVal = WriteBlock( fd, commitStrBuf, 512 + i * 512 , 512 );

  retVal = commit( fd );
  retVal = closeFile( fd );



  for( i = 0; i < 512; i++ )
    commitStrBuf[i] = '2';

  fd = openFile( "file8" );

  // write second transaction starting at offset 0                                                                                                                                           
  retVal = WriteBlock( fd, commitStrBuf, 0 , 512 );

  retVal = commit( fd );
  retVal = closeFile( fd );

  for( i = 0; i < 512; i++ )
    commitStrBuf[i] = '3';

  fd = openFile( "file8" );

  // write third transaction starting at offset 50*512                                                                                                                                       
  for (i = 0; i < 100; i++)
    retVal = WriteBlock( fd, commitStrBuf, 50 * 512 + i * 512 , 512 );

  retVal = commit( fd );
  retVal = closeFile( fd );
}


void
static appl10() {
  // test that if a server is crashed at write updates time,                                                                                                                                 
  // the library aborts the transaction at commit time                                                                                                                                       
  // the file should have only 0's in it.                                                                                                                                                    

  int fd;
  int retVal;
  int i;
  char commitStrBuf[512];

  for( i = 0; i < 256; i++ )
    commitStrBuf[i] = '0';

  fd = openFile( "file10" );

  // zero out the file first                                                                                                                                                                 
  for (i = 0; i < 100; i++)
    retVal = WriteBlock( fd, commitStrBuf, i * 256 , 256 );

  retVal = commit( fd );
  retVal = closeFile( fd );

  fd = openFile( "file10" );
  retVal = WriteBlock( fd, "111111", 0 , 6 );

  // KILL ONE OF THE  SERVERS HERE BY ISSUING A SYSTEM CALL                                                                                                                                  
  printf("kill one of the servers\n");
  sleep(5);
  printf("committing\n");

  retVal = commit( fd ); // this should return in abort                                                                                                                                      
  retVal = closeFile( fd );
}



static void
appl11() {

  // checks that a WriteBlock to a non-openFile file descriptor                                                                                                                              
  // is skipped                                                                                                                                                                              
  // There should be only 12 0's at the end in the file                                                                                                                                      

  int fd;
  int retVal;

  fd = openFile( "file11" );
  retVal = WriteBlock( fd, "000000000000", 0, 12 );

  // the following should not be performed due to wrong fd                                                                                                                                   
  retVal = WriteBlock( fd + 1, "abcdefghijkl", 0, 12 );

  retVal = commit( fd );
  retVal = closeFile( fd );

}

static void
appl14() {
  //MY-TEST: commit a file with nothing in it

    int fd;
    int retVal;

    fd = openFile( "file14" );
    //commit( fd );
    retVal = closeFile( fd );

}


static void
appl16() {
  //MY-TEST: the file should have "abcdedfghijkl" in it
    int fd;
    int retVal;

    fd = openFile( "file16" );
    retVal = WriteBlock( fd, "000000000000", 0, 12 );
    Abort( fd );
    commit ( fd );
    
    retVal = WriteBlock( fd, "abcdefghijkl", 0, 12 );
    retVal = commit( fd );
    retVal = closeFile( fd );
}


static void
appl17() {
  //MY-TEST: the file should have "abcdedfghijkl0000000000" in it
    int fd;
    int retVal;
    
    fd = openFile( "file17" );
    retVal = WriteBlock( fd, "abcdefghijkl", 0, 12 );
    retVal = WriteBlock( fd, "000000000000", 12, 12 );
    commit ( fd );

    retVal = closeFile( fd );
}

static void 
appl18() {
    int fd;
    int retVal;
    int i;
    char commitStrBuf[512];

    for( i = 0; i < 512; i++ )
        commitStrBuf[i] = '1';

    fd = openFile( "file18" );

    for (i = 0; i < 2049; i++) {
        retVal = WriteBlock( fd, commitStrBuf, i * 512 , 512 );
        if ( (i % 128) == 0 ) {
          if (i > 2048) 
            printf("here\n");
          commit ( fd );
        }
    }

    retVal = commit( fd );
    retVal = closeFile( fd );

}
