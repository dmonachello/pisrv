#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include <fcntl.h>
#include "utils.h"
#include "srvr.h"
#include "helloProtocol.h"
#include "device.h"
#include "tcpCmdSrv.h"
#include "cli.h"

ReturnStatus tcpCmdSrvGetActive(struct devStruct *);
ReturnStatus tcpCmdSrvGetEvtType(devStruct *, eventStruct *);
void tcpCmdSrvSetPolled(struct pollfd *fds, devStruct *devStructPtr, int *activeFdCountPtr);

ReturnStatus tcpCmdInProgress(devStruct *, eventStruct *);
ReturnStatus tcpCmdSrvInput(devStruct *, eventStruct *);
ReturnStatus tcpCmdSrvShutdown(devStruct *, eventStruct *);


stateTable_t tcpCmdSrvStateTable[] =
{
  // put here to start with timeout inactive
  {
       stateCmdWait,            //
       evtTimerExpired,         // wont happen
       NO_STATE_CHANGE,         //
       NO_STATE_CHANGE,         // no error state change
       NULL,                    // cleanup and wait for next command
       NULL,                    // no error handler
       NO_TIMEOUT,              // no timeout
       0
    },

    {
        stateCmdWait,           //
        evtSocketInput,         // socket input pending
        stateCmdAssemble,          //
        NO_STATE_CHANGE,        // no error state change
        &tcpCmdSrvInput,        //
        NULL,                   // no error handler
        5,                      // if a complete cmd is not assembled in a
                                // few seconds then  timeout
        0
    },



   {
       stateCmdAssemble,        //
       evtAssembleComplete,     // complete cmd line was read
       stateCmdExec,            //
       NO_STATE_CHANGE,         // no error state change
       &cliExeCmd,              //
       NULL,                    // no error handler
       NO_TIMEOUT,              // no timeout while exec cmd
       0
    },


   {
       stateCmdAssemble,        //
       evtAssembleCompletePlus, // complete cmd line was read
                                // plus some extra characters
       stateCmdExecPlus,        //
       NO_STATE_CHANGE,         // no error state change
       &cliExeCmd,              // execute the cli command
       NULL,                    // no error handler
       NO_TIMEOUT,
       0
    },

   {
        stateCmdAssemble,       //
        evtSocketInput,         // socket input pending
        stateCmdAssemble,       //
        NO_STATE_CHANGE,        // no error state change
        &tcpCmdSrvInput,        //
        NULL,                   // no error handler
        5,                      // if a complete cmd is not assembled in a
                                // few more seconds then  timeout
        0
    },

#if 0
  // this might happen if the cmd parse fails
  {
       stateCmdAssemble,            //
       evtCmdFinished,          // cmd is done executing
       stateCmdWait,            //
       NO_STATE_CHANGE,         // no error state change
       &cliCleanupCmd,          // cleanup and wait for next command
       NULL,                    // no error handler
       NO_TIMEOUT,              // no timeout
       0
    },
#endif

   {
       stateCmdAssemble,        //
       evtTimerExpired,         // didnt complete assembling cmd in time
       stateCmdWait,            //
       NO_STATE_CHANGE,         // no error state change
       &cliCleanupCmd,          // cleanup and wait for next command
       NULL,                    // no error handler
       NO_TIMEOUT,              // no timeout
       0
    },

   {
       stateCmdExec,            //
       evtCmdFinished,          // cmd is done executing
       stateCmdWait,            //
       NO_STATE_CHANGE,         // no error state change
       &cliCleanupCmd,          // cleanup and wait for next command
       NULL,                    // no error handler
       NO_TIMEOUT,              // no timeout
       0
    },

   // this event shouldn't happen in this state since we are only in this state
   // for a single pass.
   {
       stateCmdExec,            //
       evtSocketInput,          // socket input pending
       stateCmdExec,            //
       NO_STATE_CHANGE,         // no error state change
       &tcpCmdInProgress,       //
       NULL,                    // no error handler
       5,
       0
    },

   {
       stateCmdExecPlus,       //
       evtCmdFinished,         // cmd is done executing
       stateCmdAssemble,       //
       NO_STATE_CHANGE,        // no error state change
       &cliParseRemaingBuffer, // parse the extra chars in the buffer
       NULL,                   // no error handler
       5,                      // put limit on assembly time
       0
    },

   {
       stateCmdExecPlus,       //
       evtSocketInput,         // socket input pending
       stateCmdExecPlus,       //
       NO_STATE_CHANGE,        // no error state change
       &tcpCmdInProgress,      //
       NULL,                   // no error handler
       5,                      // put timeout on assembly
       0
    },

   {
       WILDCARD_STATE,         //
       evtSocketShutdown,      // socket shutdown
       stateCmdWait,           // we wont actually get to the next state since
                               // the socket and struct will be freed.
       NO_STATE_CHANGE,        // no error state change
       &tcpCmdSrvShutdown,     //
       NULL,                   // no error handler
       10,                     // 10 second timeout
       0
    },



    {stateLastState}
};

ReturnStatus tcpCmdSrvShutdown(devStruct *devStructPtr, eventStruct *evtPtr)
{
    //shutdown();
    close(devStructPtr->devFd);
    close(devStructPtr->evtFd);
    close(devStructPtr->timerFd);

    prtDevQueLen();
    remque(devStructPtr);
    prtDevQueLen();

    bzero(devStructPtr, sizeof(devStruct));
    devStructPtr->devCookie = DEV_COOKIE_DEAD;
    free(devStructPtr);

    setDevChangeFlag();

    return DEVICE_SHUTDOWN;
}

ReturnStatus tcpCmdSrvInput(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat;

#if 1
    retStat = recv(devStructPtr->devFd,
                   devStructPtr->recvBuf,
                   (sizeof(devStructPtr->recvBuf)),
                   MSG_DONTWAIT);
#else

    retStat = recv(devStructPtr->devFd,
                   &devStructPtr->recvBuf[devStructPtr->bufIdx],
                   (sizeof(devStructPtr->recvBuf) - devStructPtr->bufIdx),
                   MSG_DONTWAIT);
#endif

    if (retStat <= 0)
    {
        retStat = evtAdd(evtSocketShutdown, devStructPtr, NULL, 0);
        retStat = OK;
    }
    else
    {
        retStat = rngBufPut (devStructPtr->rngId, devStructPtr->recvBuf, retStat);

        // generic CLI routine that scans for complete cmd line.
        retStat = assembleCmdLine(devStructPtr, retStat);
    }

    return retStat;
}

//!!SID!!! I dont this will ever be called
ReturnStatus tcpCmdInProgress(devStruct *devStructPtr, eventStruct *evtPtr)
{
  return tcpCmdSrvInput(devStructPtr, evtPtr);
}

int tcpCmdSrvDevNum = 1;
ReturnStatus tcpCmdSrvCreateDevStruct(int devFd, struct sockaddr_in *ipAddr)
{
    ReturnStatus retStat;
    char devName[64];
    devStruct *devStructPtr;

    snprintf(devName, sizeof(devName), "tcpCmdSrvDev_%X_%d",
             ipAddr->sin_addr.s_addr,
             tcpCmdSrvDevNum++);
    retStat = createDevStruct(devName,
                              tcpCmdSrv,
                              tcpCmdSrvStateTable,
                              NULL,
                              &tcpCmdSrvGetActive,
                              &tcpCmdSrvSetPolled,
                              &tcpCmdSrvGetEvtType,
                              &devStructPtr);
    if (retStat == OK)
    {
        devStructPtr->devFd = devFd;
        devStructPtr->address = *ipAddr;
    }

    return retStat;
}

ReturnStatus tcpCmdSrvGetActive(struct devStruct *devStructPtr)
{
    ReturnStatus retStat = ERROR;

    retStat = devGenericGetActive(devStructPtr);

    return retStat;
}

ReturnStatus tcpCmdSrvGetEvtType(devStruct *devStructPtr, eventStruct *retEvtPtr)
{
    ReturnStatus retStat = ERROR;

    if (fdsCheck(devStructPtr->evtIdx, POLLIN))
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
    else if (fdsCheck(devStructPtr->fdsIdx, POLLIN))
    {
        retEvtPtr->eventType = evtSocketInput;
        retStat = OK;
    }
    /*
        handle these?
            POLLERR	 - Error condition.
            POLLHUP  - Hung up.
            POLLNVAL - Invalid polling request.
     */
    else if ((fdsCheck(devStructPtr->fdsIdx, POLLNVAL)) ||
             (fdsCheck(devStructPtr->fdsIdx, POLLHUP)) ||
             (fdsCheck(devStructPtr->fdsIdx, POLLERR)))
    {
        retEvtPtr->eventType = evtSocketShutdown;
        retStat = OK;
    }
    else if (fdsCheck(devStructPtr->fdsTimerIdx, POLLIN))
    {
        uint64_t timebuf;

        read(devStructPtr->timerFd, &timebuf, sizeof(timebuf));
        retEvtPtr->eventType = evtTimerExpired;
        retStat = OK;
    }
    else
    {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no tcpCmdSrv event to get\n");
    }
    return retStat;
}

void tcpCmdSrvSetPolled(struct pollfd *fds,
                        devStruct *devStructPtr,
                        int *actDevCount)
{
   devGenericSetPolled(fds, devStructPtr);
}
