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

/*
 * The hello protocol has two major functions:
 *   1. discovers and maintains list of known nodes
 *      - UDP broadcasts are sent and received to discover and keepalive
 *
 *   2. creates and maintains TCP connections to those nodes for messaging.
 *
 * A devStruct of type tcpCmdClient is used to track both of these functions.
 *
 */

// declare some global counters
int nodesAdded = 0;
int nodesDeletedFromTimer = 0;
int nodesDeletedFromError = 0;

// our local IP address. Used to discard UDP broadcasts that this node orginates
u_long myIpAddr = 0;

//trans routines
ReturnStatus helloProtocolInput(devStruct *, eventStruct *evtPtr);
ReturnStatus helloProtocolTimer(devStruct *, eventStruct *evtPtr);

#define HELLO_PROTOCOL_TIMER_VALUE 2 // 10 was goods
/*
 * A single dev - helloProtocolDev uses this state table
 */
stateTable_t helloProtocolStateTable[] =
{
   {stateProtocolActive,    //
    evtSocketInput,         // socket input pending
    stateProtocolActive,    //
    NO_STATE_CHANGE,        // no error state change
    &helloProtocolInput,    //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    0},

   {WILDCARD_STATE,         //
    evtTableTimerExpired,   // table timer expired
    NO_STATE_CHANGE,        //
    NO_STATE_CHANGE,        // no error state change
    &helloProtocolTimer,    //
    NULL,                   // no error handler
    HELLO_PROTOCOL_TIMER_VALUE, // to run the helloProtocolTimer routine
    TABLE_TIMER_FLAG},      // the table timer must run


   {stateLastState}
};

//dev struct routines
ReturnStatus helloProtocolGetEvtType(struct devStruct *, eventStruct *);
ReturnStatus helloProtocolGetActive(struct devStruct *);
void helloProtocolSetPolled(struct pollfd *, devStruct *, int *);


devStruct *helloProtocolDevStructPtr = NULL;

int helloInputCalled = 0;
int helloInputPktsRcvd = 0;
int helloInputMultipleRcvd = 0;
int helloInputRcvdErr = 0;
int helloInputRcvdOk = 0;
int helloInputNodeMatch = 0;
int helloInputNoNodeMatch = 0;
int helloInputTcpCmdClientErr = 0;
int helloInputTcpCmdClientInitOk = 0;

// get hello messages from the other nodes and add new ones to the node list
ReturnStatus helloProtocolInput(devStruct *devStructPtr, eventStruct *evtPtr)
{
  int len;
  devStruct *ptr;
  char mesg[1000];
  struct sockaddr_in cliaddr;
  int retLen;
  int loopCnt;
  int retStat;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
         "socket input\n");

  helloInputCalled += 1;
  for(loopCnt=0;;loopCnt++)
  {
    len = sizeof(cliaddr);

    // we don't really care about the content of the message. we just want to
    // know that the node is alive.
    retLen = recvfrom(helloProtocolDevStructPtr->devFd,
  		    mesg,
		    sizeof(mesg),
		    0,
		    (struct sockaddr *)&cliaddr,
		    (socklen_t *)&len);

    if (retLen <= 0)
    {
      if (loopCnt > 1)
      {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
               "helloProtocolInput exit loopCnt multiple packets (%d) received\n",
               loopCnt);
      }
      else
      {
          helloInputRcvdErr += 1;
      }
      break;
    }
    else
    {

      helloInputRcvdOk += 1;
      // was the message echoed back to us?
      if (myIpAddr == cliaddr.sin_addr.s_addr)
      {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "from us. ignore it\n");
      }
      else
      {
        // look for the node in the list to keep it alive
        for (ptr = getNextNode(NULL, tcpCmdClient);
           ptr != NULL;
           ptr = getNextNode(ptr, tcpCmdClient))
        {
          if (ptr->address.sin_addr.s_addr == cliaddr.sin_addr.s_addr)
          {
            ptr->pingCount += 1;
            ptr->activeCount = 0;
            logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
                   "node %p IP Address %lx already in list (%d %d)\n",
	               ptr, ptr->address.sin_addr.s_addr,
	               ptr->pingCount, ptr->activeCount);
            helloInputNodeMatch += 1;
            break;
          }
        }// end for

        if (ptr == NULL)
        {
            helloInputNoNodeMatch += 1;
          // we don't know this node so make a connection to it.
          retStat = tcpCmdClientInit(cliaddr);
          if (retStat != OK)
          {
            logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
                   "tcpCmdClientInit fail\n");
            helloInputTcpCmdClientErr += 1;
          }
          else
          {
              helloInputTcpCmdClientInitOk += 1;
              // make sure we get in the fds list
              setDevChangeFlag();
          }
	    }
      }
    }
  }// end while
  return OK;
}

// transrtn that is called when timer expires. Is used to send hello
//  bcasts and timeout nodes we heven't heard from in a while.
ReturnStatus helloProtocolTimer(devStruct *devStructPtr, eventStruct *evtPtr)
{
//  uint64_t timebuf;
  struct sockaddr_in servaddr;
  char mesg[1000];
  int buflen;
  ReturnStatus retStat = OK;
  int sendLen;
  int len;
  devStruct *ptr;
  devStruct *nextPtr;
  int sendStat;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "timer expired\n");

  // read timer value and throw it away. need to do this to reset the fd
  // I think that since we always cancel the timer  before calling transrtns
  // we don't need to do this. keep this code here but commented out until we
  // know this is true.
  //read(devStructPtr->timerFd, &timebuf, sizeof(timebuf));

  // send a bcast hello to everyone
  servaddr.sin_addr.s_addr=htonl(-1); /* send message to 255.255.255.255 */
  servaddr.sin_port = htons(PISRV_PORT_NUM); /* port number */
  servaddr.sin_family = PF_INET;

  strncpy(mesg, "Hello", strlen("Hello"));
#if 1
  buflen = strlen(mesg);
  len = sizeof(servaddr);
  // !!!SID!!! DEBUG - don't send this on corp network!!!!
  sendLen = sendto(devStructPtr->devFd,
                   mesg,
                   buflen, 0,
                   (struct sockaddr *)&servaddr, len);
  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "sendto Status = %d\n", sendLen);
#else
#warning "code disabled for work build!!!"
#endif

  // send a message to all cmd servers we are connected to
  // this should catch all dead TCP connections
  // ???

  // get rid of expired nodes
  for (ptr = getNextNode(NULL, tcpCmdClient),
       nextPtr = getNextNode(ptr, tcpCmdClient);
       ptr != NULL;
       ptr = nextPtr,
       nextPtr = getNextNode(ptr, tcpCmdClient))
  {
    // send a ping req to the server
    //logmsg(ptr->devFd, 0, LOG_LVL_NORMAL, "pingReq\r");
    sendStat = send(ptr->devFd,
                   "pingReq\n\r",
                   strlen("pingReq\n\r"),
                   0);
    ptr->activeCount += 1;
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
           "helloProtocolTimer send ping stat %d\n", sendStat);

    if (ptr->activeCount > HELLO_NODE_LIMIT)
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
             "removing node %p %lx %d\n",
             ptr, ptr->address.sin_addr.s_addr,
             ptr->activeCount);
      tcpCmdClientShutdown(ptr, NULL);
    }
    else
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "keep node %p %lx %d\n",
             ptr, ptr->address.sin_addr.s_addr, ptr->activeCount);
    }
  }
  return retStat;
}

void helloProtocolSetPolled(struct pollfd *fds, devStruct *devStructPtr, int *activeFdCountPtr)
{
  devGenericSetPolled(fds, devStructPtr);
}



ReturnStatus helloProtocolGetActive(struct devStruct *devStructPtr)
{
   ReturnStatus retStat = ERROR;
   retStat = devGenericGetActive(devStructPtr);
   return retStat;
}

ReturnStatus helloProtocolGetEvtType(struct devStruct *devStructPtr, eventStruct *retEvtPtr)
{
   ReturnStatus retStat = ERROR;

   retStat = devGenericGetEvtType(devStructPtr, retEvtPtr);

   return retStat;
}



ReturnStatus getMyAddr(char *netDevStr)
{
  ReturnStatus retStat = -1;
  struct ifreq ifr;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  if (sockfd >= 0)
  {
      /* I want to get an IPv4 IP address */
      ifr.ifr_addr.sa_family = AF_INET;

      strncpy(ifr.ifr_name, netDevStr, IFNAMSIZ-1);

      /* get the IP address attached to this device */
      if (ioctl(sockfd, SIOCGIFADDR, &ifr) == 0)
      {
        retStat = 0;
        myIpAddr = ((((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr));

        /* display result */
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "%lx %s\n",
               myIpAddr, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
      }

      close(sockfd);
  }

  if (retStat != 0)
  {
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS, "Failed to get IP address for device %s\n", netDevStr);
  }

  return retStat;
}


ReturnStatus helloProtocolInit()
{
  ReturnStatus retStat = ERROR;
  struct sockaddr_in servaddr;
  int yes=1;
  int nonBlocking = 1;

  retStat = getMyAddr("wlan0");
  if (retStat != 0)
    retStat = getMyAddr("eth0");

  retStat = createDevStruct("helloProtocolDev",
                            hello,
                            helloProtocolStateTable,
                            NULL,
                            &helloProtocolGetActive,
                            &helloProtocolSetPolled,
                            &helloProtocolGetEvtType,
                            &helloProtocolDevStructPtr);

  helloProtocolDevStructPtr->devFd=socket(AF_INET,SOCK_DGRAM, IPPROTO_UDP);
  if (helloProtocolDevStructPtr->devFd > 0)
  {
    int on = 1;
    retStat = setsockopt(helloProtocolDevStructPtr->devFd,
                         SOL_SOCKET,
                         SO_REUSEADDR,
                         &on,
                         sizeof(on));

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    servaddr.sin_port=htons(PISRV_PORT_NUM);
    bind(helloProtocolDevStructPtr->devFd,
         (struct sockaddr *)&servaddr,
         sizeof(servaddr));

    retStat = setsockopt(helloProtocolDevStructPtr->devFd,
                         SOL_SOCKET, SO_BROADCAST, &yes, sizeof(int) );
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "Setsockopt Status = %d\n", retStat);

    if (fcntl(helloProtocolDevStructPtr->devFd, F_SETFL,
              O_NONBLOCK, nonBlocking ) == ERROR)
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "failed to set non-blocking socket\n" );
      retStat = ERROR;
    }


    if (retStat < OK)
    {
      close(helloProtocolDevStructPtr->devFd);
      helloProtocolDevStructPtr->devFd = ERROR;
    }
  }

  return retStat;
}

void prtNodeList(devStruct *devStructPtr)
{
  devStruct *ptr;

#if 0
  logmsg(0, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "queue length %d",
         queLen(&nodeList));
#endif

  logmsg(0, ADD_CR_FLAG, LOG_LVL_ALWAYS, "print list");
  for (ptr = getNextNode(NULL, tcpCmdClient);
       ptr != NULL;
       ptr = getNextNode(ptr, tcpCmdClient))
  {
    logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
           "node %p IP Address %lx pingCount %d %d",
           ptr,
           ptr->address.sin_addr.s_addr,
           ptr->pingCount,
           ptr->activeCount);
  }
  logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
         "nodes added %d deleted timer %d error %d",
         nodesAdded, nodesDeletedFromTimer, nodesDeletedFromError);
}
