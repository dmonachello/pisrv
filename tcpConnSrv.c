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
#include "tcpCmdSrv.h"

ReturnStatus tcpConnSrvAcceptConnection(devStruct *, eventStruct *);

ReturnStatus tcpConnSrvGetActive(struct devStruct *);
ReturnStatus tcpConnSrvrGetEvtType(devStruct *, eventStruct *);
void tcpConnSrvSetPolled(struct pollfd *, devStruct *, int *);

stateTable_t tcpConnSrvStateTable[] =
{
   {stateProtocolActive,    //
    evtSocketInput,         // socket input pending
    stateProtocolActive,    //
    NO_STATE_CHANGE,        // no error state change
    &tcpConnSrvAcceptConnection,            //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    0},

   {stateLastState}
};

devStruct *tcpConnSrvDevStruct;

ReturnStatus tcpConnSrvAcceptConnection(devStruct *devStructPtr, eventStruct *evtPtr)
{
  devStruct *ptr;
  struct sockaddr_in cliaddr;
//  struct sockaddr tstAddr;
  int len = sizeof(cliaddr);
  ReturnStatus retStat = OK;

  // Accept the incoming connection from the client.
  int newsockfd = accept(tcpConnSrvDevStruct->devFd,
                         (struct sockaddr *)&cliaddr,
                         (socklen_t *)&len);

  if (newsockfd < 0)
  {
      perror("ERROR on accept");
  }
  else
  {

      // find the node that connected to us
      for (ptr  = getNextNode(NULL, tcpCmdClient);
	       ptr != NULL;
	       ptr  = getNextNode(ptr, tcpCmdClient))
      {
	    // match the ip address
	    if (ptr->address.sin_addr.s_addr == cliaddr.sin_addr.s_addr)
        {
          // remember the socket fd to use in the reliable bcast.
          // don't forget to clean this up if  the matching tcpclient
          // exits.
	      // ptr->devFd = newsockfd; //!!!SID!!! DEBUG - I think this is wrong!!!
	      break;
	    }
      } // end for loop
#if 0
      if (ptr == NULL)
      {
  	      // we didn't find a match so close it
	      close(newsockfd);
      }
      else
#endif
      {
          int flag = 1;

          // create a new device struct for this connection
          retStat = tcpCmdSrvCreateDevStruct(newsockfd,
                                             &cliaddr);
          retStat = setsockopt(newsockfd,
                               IPPROTO_TCP,
                               TCP_NODELAY,
                               (char *) &flag,
                               sizeof(flag));
          // set the flag
          setDevChangeFlag();

      }
  }
  return retStat;
}

ReturnStatus tcpConnSrvGetActive(struct devStruct *devStructPtr)
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
     logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no tcp fd ready\n");
   }
   return retStat;
}

ReturnStatus tcpConnSrvrGetEvtType(devStruct *devStructPtr, eventStruct *retEvtPtr)
{
   ReturnStatus retStat = ERROR;

   if (fdsCheck(devStructPtr->fdsIdx, POLLIN))
   {
      retEvtPtr->eventType = evtSocketInput;
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
       logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no tcpsrv event to get\n");
   }
   return retStat;
}

void tcpConnSrvSetPolled(struct pollfd *fds, devStruct *devStructPtr, int *activeFdCountPtr)
{
    devGenericSetPolled(fds, devStructPtr);
}

ReturnStatus tcpConnSrvInitSock()
{
  ReturnStatus retStat = ERROR;
  struct sockaddr_in servaddr;
  unsigned short portno = PISRV_PORT_NUM;


  retStat = createDevStruct("tcpConnSrvDev",
                            tcpConnSrv,
                            tcpConnSrvStateTable,
                            NULL,
                            &tcpConnSrvGetActive,
                            &tcpConnSrvSetPolled,
                            &tcpConnSrvrGetEvtType,
                            &tcpConnSrvDevStruct);
  if (retStat == OK)
  {
      tcpConnSrvDevStruct->devFd=socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
      if (tcpConnSrvDevStruct->devFd > OK)
      {
        int on = 1;
        retStat = setsockopt(tcpConnSrvDevStruct->devFd,
                             SOL_SOCKET,
                             SO_REUSEADDR,
                             &on,
                             sizeof(on));

        /* Initialize socket structure */
        bzero((char *) &servaddr, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(portno);

        /* Now bind the host address using bind() call.*/
        if (bind(tcpConnSrvDevStruct->devFd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < OK)
        {
          perror("ERROR on binding");
          close(tcpConnSrvDevStruct->devFd);
          tcpConnSrvDevStruct->devFd = -1;
        }
        else
        {
          /* Now start listening for the clients */
          listen(tcpConnSrvDevStruct->devFd,5);
          retStat = OK;

        }
    }
  }

  return retStat;
}
