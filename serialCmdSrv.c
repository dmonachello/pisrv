#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <unistd.h>    //Used for UART
#include <fcntl.h>     //Used for UART
#include <termios.h>   //Used for UART

#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <netinet/in.h>

#include <fcntl.h>
#include "utils.h"
#include "srvr.h"
#include "serialCmdSrv.h"
#include "device.h"
#include "cli.h"

//trans routines
ReturnStatus serialInput(devStruct *, eventStruct *evtPtr);
ReturnStatus serialTimer(devStruct *, eventStruct *evtPtr);

stateTable_t serialStateTable[] =
{
   {stateProtocolActive,    //
    evtSerialInput,         // socket input pending
    stateProtocolActive,    //
    NO_STATE_CHANGE,        // no error state change
    &serialInput,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},

   {stateProtocolActive,    //
    evtTimerExpired,        // timer expired
    stateProtocolActive,    //
    NO_STATE_CHANGE,        // no error state change
    &serialTimer,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},

   {stateProtocolActive,    //
    evtAssembleComplete,        // timer expired
    stateProtocolActive,    //
    NO_STATE_CHANGE,        // no error state change
    &cliExeCmd,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},


   {stateLastState}
};

//dev struct routines
ReturnStatus serialGetEvtType(struct devStruct *, eventStruct *);
ReturnStatus serialGetActive(struct devStruct *);
void serialSetPolled(struct pollfd *, devStruct *, int *);


devStruct *serialDevStructPtr = NULL;

ReturnStatus serialInput(devStruct *devStructPtr, eventStruct *evtPtr)
{
  int retLen;
  int loopCnt;
  ReturnStatus retStat = OK;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "serial input\n");

  for(loopCnt=0;;loopCnt++)
  {
	retLen = read(serialDevStructPtr->devFd,
                  (void*)&serialDevStructPtr->recvBuf[devStructPtr->bufIdx],
                  (sizeof(serialDevStructPtr->recvBuf) - devStructPtr->bufIdx));

    if (retLen <= 0)
    {
      if (loopCnt > 1)
      {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "serialInput exit loopCnt multiple characters (%d) received\n", loopCnt);
      }
      break;
    }
    else
    {
        retStat = assembleCmdLine(serialDevStructPtr, retLen);
    }
  }// end for
  return retStat;
}

// transrtn that is called when timer expires.
ReturnStatus serialTimer(
                         devStruct *devStructPtr,
                         eventStruct *evtPtr
                        )
{
  uint64_t timebuf;
  ReturnStatus retStat = OK;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "serial timer expired\n");

  // read timer value and throw it away. need to do this to reset the fd
  // I think that since we always cancel the timer  before calling transrtns
  // we don't need to do this. keep this code here but commented out until we
  // know this is true.
  read(devStructPtr->timerFd, &timebuf, sizeof(timebuf));

  // !!!SID!!! TBD - should we implement a keepalive on the serial connection?

  return retStat;
}

void serialSetPolled(
                     struct pollfd *fds,
                     devStruct *devStructPtr,
                     int *activeFdCountPtr
                     )
{
  devGenericSetPolled(fds, devStructPtr);
}


ReturnStatus serialGetActive(struct devStruct *devStructPtr)
{
   ReturnStatus retStat = ERROR;

   if (fdsCheck(devStructPtr->fdsIdx, POLLIN))
   {
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsTimerIdx, POLLIN))
   {
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->evtIdx, POLLIN))
   {
      retStat = OK;
   }
   else
   {
     logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no serial func ready\n");
   }
   return retStat;
}

ReturnStatus serialGetEvtType(struct devStruct *devStructPtr, eventStruct *retEvtPtr)
{
   ReturnStatus retStat = ERROR;

   if (fdsCheck(devStructPtr->fdsIdx, POLLIN))
   {
      retEvtPtr->eventType = evtSerialInput;
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsTimerIdx, POLLIN))
   {
      uint64_t timebuf;

      read(devStructPtr->timerFd, &timebuf, sizeof(timebuf));
      retEvtPtr->eventType = evtTimerExpired;
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->evtIdx, POLLIN))
   {
     eventStruct *evtPtr;

     evtPtr = (eventStruct *)getNext(NULL, &devStructPtr->EvtStructList);
     if (evtPtr != NULL)
     {
         uint64_t cnt;

         remque(evtPtr);
         retStat = read(devStructPtr->evtFd,
                        &cnt,
                        sizeof(cnt));
     }
     *retEvtPtr = *evtPtr;
      retStat = OK;
   }
   else
   {
       logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "no serial event to get\n");
   }
   return retStat;
}

ReturnStatus initSerial()
{
  ReturnStatus retStat = ERROR;

  retStat = createDevStruct("serialDev",
                            serial,
                            serialStateTable,
                            NULL,
                            &serialGetActive,
                            &serialSetPolled,
                            &serialGetEvtType,
                            &serialDevStructPtr);
  if (retStat == OK)
  {
     serialDevStructPtr->devFd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY | O_NDELAY);//Open in non blocking read/write mode
     if (serialDevStructPtr->devFd > 0)
     {
        struct termios options;

        tcgetattr(serialDevStructPtr->devFd, &options);
        options.c_cflag = B115200 | CS8 | CLOCAL | CREAD;//<Set baud rate
        options.c_iflag = IGNPAR;
        options.c_oflag = 0;
        options.c_lflag = 0;
        tcflush(serialDevStructPtr->devFd, TCIFLUSH);
        tcsetattr(serialDevStructPtr->devFd, TCSANOW, &options);
     }
  }
  return retStat;
}

void prtSerial()
{
  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "serial port characteristics ???\n");
}
