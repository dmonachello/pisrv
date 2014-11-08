#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

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

/*
 * The hello protocol has two major functions:
 *   1. discovers and maintains list of known nodes
 *   2. creates and maintains TCP connections to those nodes for messaging.
 *
 */

void tcpCmdClientSetPolled(struct pollfd *fds, devStruct *devStructPtr, int *activeFdCountPtr)
{
  devGenericSetPolled(fds, devStructPtr);
}

ReturnStatus tcpCmdClientGetActive(struct devStruct *devStructPtr)
{
   ReturnStatus retStat = ERROR;
   retStat = devGenericGetActive(devStructPtr);
   return retStat;
}

ReturnStatus tcpCmdClientGetEvtType(struct devStruct *devStructPtr, eventStruct *retEvtPtr)
{
   ReturnStatus retStat = ERROR;

   retStat = devGenericGetEvtType(devStructPtr, retEvtPtr);

   return retStat;
}

// receive request and reply msgs from the msgAPI from other nodes here
ReturnStatus tcpCmdClientInput(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;
    extern devStruct *msgApiDevPtr;

    // receive the message
    retStat = recv(devStructPtr->devFd,
                   devStructPtr->recvBuf,
                   sizeof(devStructPtr->recvBuf),
                   MSG_DONTWAIT);

    if (retStat <= 0)
    {
        // received an error so queue a rsp err event
        if (msgApiDevPtr != NULL)
            retStat = evtAdd(evtOneMsgBad, msgApiDevPtr, NULL, 0);

        retStat = evtAdd(evtSocketShutdown, devStructPtr, NULL, 0);

        retStat = OK;
    }
    else
    {
        if (retStat < (sizeof(devStructPtr->recvBuf)) - 1)
        {
            int idx;
            devStructPtr->recvBuf[retStat] = '\0';
            logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
                   "!!!!****tcpCmdClientInput - len %d buf %s",
                   retStat, devStructPtr->recvBuf);
            for (idx=0;idx<retStat;idx++)
            {
                printf("%2.2x", devStructPtr->recvBuf[idx]);
            }
            printf("\n");
       }
        // received OK so queue a rsp OK event
        if (msgApiDevPtr != NULL)
            retStat = evtAdd(evtOneMsgOK, msgApiDevPtr, devStructPtr->recvBuf, retStat);
        retStat = OK;
    }

    return retStat;
}

ReturnStatus tcpCmdClientShutdown(devStruct *devStructPtr, eventStruct *evtPtr)
{
    extern int nodesDeletedFromError;
    extern int nodesDeletedFromTimer;

    /*
     * If called from FSM then evtPtr will not be null and cause will be a device error.
     * Otherwise called from hello protocol with evtPtr == NULL
     */
    if (evtPtr != NULL)
    {
        nodesDeletedFromError += 1;
    }
    else
    {
        nodesDeletedFromTimer += 1;
    }
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

ReturnStatus tcpCmdClientTimer(devStruct *devStructPtr, eventStruct *evtPtr)
{
    return OK;
}


stateTable_t tcpCmdClientStateTable[] =
{
   {stateNodeActive,    //
    evtSocketInput,         // socket input pending
    stateNodeActive,    //
    NO_STATE_CHANGE,        // no error state change
    &tcpCmdClientInput,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},

   {stateNodeActive,    //
    evtTimerExpired,        // timer expired
    stateNodeActive,    //
    NO_STATE_CHANGE,        // no error state change
    &tcpCmdClientTimer,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},

   {stateNodeActive,    //
    evtSocketShutdown,        // timer expired
    stateNodeActive,    //
    NO_STATE_CHANGE,        // no error state change
    &tcpCmdClientShutdown,            //
    NULL,                   // no error handler
    10,                     // 10 second timeout
    0},

   {stateLastState}
};


ReturnStatus tcpCmdClientInit(struct sockaddr_in cliaddr)
{
  char devName[32];
  ReturnStatus retStat;
  devStruct *newNode;
  extern int nodesAdded;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
         "new ip address received %lx\n",
         (unsigned long)cliaddr.sin_addr.s_addr);

//----
  snprintf(devName, sizeof(devName), "tcpCmdClient_%X_%d",
           cliaddr.sin_addr.s_addr,
           nodesAdded++);

  retStat = createDevStruct(devName,
                    tcpCmdClient,
                    tcpCmdClientStateTable,
                    NULL,
                    &tcpCmdClientGetActive,
                    &tcpCmdClientSetPolled,
                    &tcpCmdClientGetEvtType,
                    &newNode);

//----
  if (retStat != OK)
  {
      ;
  }
  else
  {
    newNode->address = cliaddr;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
           "adding IP addr %lx node %p to list\n",
           newNode->address.sin_addr.s_addr, newNode);

    // create a new socket to talk to the new node. All outgoing messages
    // go through this socket.
    if ((newNode->devFd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP)) <= 0)
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ERROR,
             "ERROR creating new socket\n");
    }
    else
    {
       // make sure the port num for this TCP connection is for the tcpCmdSrv
       newNode->address.sin_port=htons(PISRV_PORT_NUM);

      // create a new TCP connection to the new nodes comand server.
      if (connect(newNode->devFd,
                  (struct sockaddr *)&newNode->address,
                  sizeof(newNode->address)) < 0)
      {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ERROR,
               "ERROR connecting to node %lx\n",
               (unsigned long)cliaddr.sin_addr.s_addr);
        retStat = ERROR;
      }
      else
      {
          int flag = 1;

          retStat = setsockopt(newNode->devFd,
                               IPPROTO_TCP,
                               TCP_NODELAY,
                               (char *) &flag,
                               sizeof(flag));
      }
    }
  }
  return retStat;
}
