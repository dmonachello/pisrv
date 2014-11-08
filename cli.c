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

cliDef_t cmdArray[] =
{
    {cmdTest,       "test",         2},
    {cmdPrtDevs,    "prtdevs",      0},
    {cmdHelp,       "help",         0},
    {cmdPrtFds,     "prtfds",       0},
    {cmdPrtNodes,   "prtnodes",     0},
    {cmdSetLogLvl,  "setloglvl",    1},
    {cmdPingReq,    "pingReq",   0},
    {cmdPrtStats,   "prtStats",  0},
    {cmdRemCmd,     "remCmd",    2},
    {cmdLast}
};

//send command to speicified remote node to execute
ReturnStatus cmdRemCommand(devStruct *devStructPtr, char *remNodeId, char *cmdStr)
{
    ReturnStatus retstat = OK;
    struct sockaddr_in addr;
    devStruct *ptr;

    // get the remote IP address
    addr = inet_addr(remNodeId);

    // find the tcpCmdClient for this remote node
    for (ptr = getNextNode(NULL, tcpCmdClient);
         ptr != NULL;
         ptr = getNextNode(ptr, tcpCmdClient))
    {
        if (ptr->address.sin_addr.s_addr == addr.sin_addr.s_addr)
        {
            break;
        }
    }


    // if found and not busy then put the args in the tcpCmdClient dev struct
    if (ptr != NULL)
    {
        int idx;
        int offset;

        // put the parse info from the current tcp server dev into the tcp client dev
        bcopy(devStructPtr->recvBuf, ptr->recvBuf, sizeof(ptr->recvBuf));
        ptr->argCount = devStructPtr->argCount;


        for(idx=0;idx<ptr->argCount;idx++)
        {
            offset = devStructPtr->argPtrs[idx] - devStructPtr->recvBuf;
            ptr->argPtrs[idx] = ptr->recvBuf + offset;
        }

        // queue an event on the tcpCmdClient to send the command
        evtAdd(evtRemoteCmdStart, ptr, NULL, 0);

        // put an event on this tcpCmdSrv dev to indicate it is involved in executing a remote command.
        evtAdd(evtRemoteCmdStart, devStructPtr, NULL, 0);

    }

    return retstat;
}

void prtStats(devStruct *devStructPtr)
{

  extern int helloInputCalled;
  extern int helloInputPktsRcvd;
  extern int helloInputMultipleRcvd;
  extern int helloInputRcvdErr;
  extern int helloInputRcvdOk;
  extern int helloInputNodeMatch;
  extern int helloInputNoNodeMatch;
  extern int helloInputTcpCmdClientErr;
  extern int helloInputTcpCmdClientInitOk;
  extern int pollPriEvtCnt;

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputCalled %d\n",
         helloInputCalled);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputPktsRcvd %d\n",
         helloInputPktsRcvd);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputMultipleRcvd %d\n",
         helloInputMultipleRcvd);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputRcvdErr %d\n",
         helloInputRcvdErr);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputRcvdOk %d\n",
         helloInputRcvdOk);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputNodeMatch %d\n",
          helloInputNodeMatch);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputNoNodeMatch %d\n",
         helloInputNoNodeMatch);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputTcpCmdClientErr %d\n",
         helloInputTcpCmdClientErr);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "helloInputTcpCmdClientInitOk %d\n",
         helloInputTcpCmdClientInitOk);

  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "pollPriEvtCnt %d\n",
         pollPriEvtCnt);


}

#define DELIMITERS " \r"
ReturnStatus parseCmd(devStruct *devStructPtr, eventStruct *evtPtr, cmdEnum_t *cliNumPtr)
{
    ReturnStatus retStat = ERROR;
    int idx = 0;

    *cliNumPtr = cmdNone;

    devStructPtr->bufPtr = devStructPtr->recvBuf;

    // parse the command line

    // get pointers to the command name and args
    for (devStructPtr->nextArg = strtok_r(devStructPtr->bufPtr,
                                          DELIMITERS,
                                          &devStructPtr->savePtr);
         devStructPtr->nextArg != NULL;
         devStructPtr->nextArg = strtok_r(NULL,
                                          DELIMITERS,
                                          &devStructPtr->savePtr))
    {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "%d. %s\n",
               idx,
               devStructPtr->nextArg);
        if (devStructPtr->nextArg[0] != '\n')
        {
            devStructPtr->argPtrs[idx++] = devStructPtr->nextArg;
        }
    }

    // minus 1 for cmd name
    devStructPtr->argCount = idx - 1;

    // find the command
    for (idx=0; cmdArray[idx].cmdNum != cmdLast;idx++)
    {
        // cmd name is in the 0 ptr. if null get out.
        if (devStructPtr->argPtrs[0] == NULL)
        {
            break;
        }

        if (!(strncmp(devStructPtr->argPtrs[0],
                      cmdArray[idx].cmdStr,
                      strlen(cmdArray[idx].cmdStr))))
        {
          *cliNumPtr = cmdArray[idx].cmdNum;
          break;
        }
    }

    // make sure there are enough args
    if ((*cliNumPtr != cmdNone) &&
        (devStructPtr->argCount == cmdArray[idx].maxArgs))
    {
        retStat = OK;
    }
    else
    {
        retStat = retStat;
    }

    return retStat;
}

ReturnStatus cliExeCmd(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;
    int idx = 0;
    cmdEnum_t cliNum = cmdNone;
    devStruct *tmpStructPtr;

    retStat = parseCmd(devStructPtr, evtPtr, &cliNum);

    //
    if (retStat == OK)
    {
        //execute the command
        switch(cliNum)
        {
            case cmdPingReq:
#if 0
              logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL, "pingReq");
#endif
              logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL,
                    "pong");
              break;

            case cmdTest:
              logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "cmdTest\n");
              break;

            case cmdRemCmd:
                cmdRemCommand(devStructPtr, devStructPtr->argPtrs[1], devStructPtr->argPtrs[2]);
                break;

            case cmdPrtStats:
                prtStats(devStructPtr);
                break;


            case cmdSetLogLvl:
                setLogLvl(devStructPtr->argPtrs[1]);
                break;

            case cmdPrtFds:

                prtFds(devStructPtr);

                break;

            case cmdPrtNodes:

                prtNodeList(devStructPtr);

                break;

            case cmdPrtDevs:
                for (tmpStructPtr  = getNextNode(NULL, allDevs);
                     tmpStructPtr != NULL;
                     tmpStructPtr  = getNextNode(tmpStructPtr, allDevs))
                {
                    // output back to device input came in on
                    logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL,
                           "device: %s\r\ncookie: %X\r\ntype: %x\r\nstate: %x ",
                           tmpStructPtr->devName,
                           tmpStructPtr->devCookie,
                           tmpStructPtr->devType,
                           tmpStructPtr->state);

                    logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL,
                           "devFd: %d\r\nevtFd: %d\r\ntimerFd: %d\r\ntableTimerFd: %d\r\naddress %X ",
                           tmpStructPtr->devFd,
                           tmpStructPtr->evtFd,
                           tmpStructPtr->timerFd,
                           tmpStructPtr->tableTimerFd,
                           tmpStructPtr->address.sin_addr.s_addr);
                }
                break;

            case cmdHelp:
                for (idx=0; cmdArray[idx].cmdNum != cmdLast;idx++)
                {
                    logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL,
                           cmdArray[idx].cmdStr);
                }
                break;

            default:
              logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_NORMAL,
                     "unknown cmd %s",
                     devStructPtr->argPtrs[0]);
              retStat = ERROR;
        }//end switch

    }

    evtAdd(evtCmdFinished, devStructPtr, NULL, 0);

    if (retStat == OK)
    {
        // send the good response back
        logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
               "%s OK",
               devStructPtr->argPtrs[0]);
    }
    else
    {
       // send the fail response back
        logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
               "%s FAILED ",
               devStructPtr->argPtrs[0]);

        // always return a good status to move the state machine along
        retStat = OK;
    }

    // cleanup
    resetCmdLine(devStructPtr);

    return retStat;
}

ReturnStatus cliCleanupCmd(devStruct *devStructPtr, eventStruct *evtStructPtr)
{
    ReturnStatus retStat = OK;

    devStructPtr->bufIdx = 0;

    return retStat;
}
ReturnStatus cliParseRemaingBuffer(devStruct *devStructPtr, eventStruct *evtStructPtr)
{
    ReturnStatus retStat;

    // try to assmble another cmd with the chars left in the read buffer
    retStat = assembleCmdLine(devStructPtr, 0);

    return retStat;
}

// new chars were already put into the recvBuf. Account for the new
// chars and look for the line terminator. If found then queue a
// command line complete event.
ReturnStatus assembleCmdLine(devStruct *devStructPtr, int readLen)
{
    //int endCurCmd;
    ReturnStatus retStat = OK;
    int nChars;

#if 0
    devStructPtr->bufIdx += readLen;
    devStructPtr->recvBuf[devStructPtr->bufIdx] = '\0';

    // look for the line terminator
    devStructPtr->nextArg = strchr(devStructPtr->bufPtr, '\r');

    if (devStructPtr->nextArg != NULL)
#else
    retStat = rngSearch(devStructPtr->rngId, '\r', &nChars);
    printf("rngSearch retStat %d nchars %d\n", retStat, nChars);
    if ((retStat == 0) && (nChars > 0))
#endif
    {

        retStat = rngBufGet (devStructPtr->rngId, devStructPtr->recvBuf, nChars+1);
        devStructPtr->recvBuf[nChars-1] = '\0';

        // command line is complete
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
               "cmd line completed - %s\n",
               devStructPtr->recvBuf);


#if 0
        // if there are chars after the terminator then event is evtAssembleCompletePlus
        // +1 to account for terminator
        //endCurCmd = (devStructPtr->nextArg - devStructPtr->bufPtr) + 1;
        //endCurCmd = strspn(&devStructPtr->bufPtr[devStructPtr->bufIdx], "\r\n ");
        //!!!SID!!! TBD - this needs to be tested!!!
        if (*(devStructPtr->nextArg+1) == '\n')
          devStructPtr->nextArg += 1;

        if (*(devStructPtr->nextArg+1) == '\0')
#else
         // if there are chars still in the buffer then event is evtAssembleCompletePlus
         if (rngNBytes(devStructPtr->rngId) > 0)
#endif
        {
            // queue a command line assembly complete plus event
            retStat = evtAdd(evtAssembleCompletePlus, devStructPtr, NULL, 0);
        }
        else
        {
            retStat = evtAdd(evtAssembleComplete, devStructPtr, NULL, 0);
        }
    }

    return retStat;
}

void resetCmdLine(devStruct *devStructPtr)
{
    devStructPtr->bufPtr = devStructPtr->recvBuf;
    devStructPtr->bufIdx = 0;
}
