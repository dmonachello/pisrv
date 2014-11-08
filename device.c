#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/eventfd.h>


#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include <fcntl.h>
#include <assert.h>

#include "utils.h"
#include "srvr.h"
#include "device.h"
#include "helloProtocol.h"
#include "tcpConnSrv.h"
#include "serialCmdSrv.h"
#include "cli.h"

globalDeviceStruct globalDevStructArray;
queElem devStructList = {&devStructList, &devStructList};
int pollPriEvtCnt = 0;

void prtDevQueLen()
{
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS,
           "devque length %d\n", queLen(&devStructList));
}

// create a new devStruct structure and return a pointer to the new structure
// in retDevStructPtr. The new devStruct is also linked into the devStructList list.
// return OK if it works or ERROR is it fails.
ReturnStatus createDevStruct(char *inDevName,
                             devType_t devType,
                             stateTable_t *inStateTablePtr,
                             devInit inDevInitPtr,
                             devGetActive inDevGetActivePtr,
                             devSetPolled inDevSetPolledPtr,
                             devGetEvt inDevGetEvtPtr,
                             devStruct **retDevStructPtr)
{
    devStruct *devStructPtr;
    ReturnStatus retStat = OK;

    devStructPtr = calloc(1, sizeof(devStruct));
    if (devStructPtr == NULL)
    {
        retStat = ERROR;
    }
    else
    {
        strncpy(devStructPtr->devName, inDevName, sizeof(devStructPtr->devName));

        devStructPtr->devCookie = DEV_COOKIE_LIVE;

        devStructPtr->devType          = devType;
        devStructPtr->devFd            = INVALID_FD;
        devStructPtr->timerFd          = INVALID_FD;
        devStructPtr->tableTimerFd     = INVALID_FD;
        devStructPtr->evtFd            = INVALID_FD;

        devStructPtr->fdsIdx           = NO_FDS_IDX;
        devStructPtr->fdsTimerIdx      = NO_FDS_IDX;
        devStructPtr->fdsTableTimerIdx = NO_FDS_IDX;
        devStructPtr->evtIdx           = NO_FDS_IDX;

        // remember the state table for this device
        devStructPtr->stateTablePtr   = inStateTablePtr;

        // set the function pointers
        devStructPtr->devInitPtr      = inDevInitPtr;
        devStructPtr->devGetActivePtr = inDevGetActivePtr;
        devStructPtr->devSetPolledPtr = inDevSetPolledPtr;
        devStructPtr->devGetEvtPtr    = inDevGetEvtPtr;
        devStructPtr->evtFd           = eventfd(0, EFD_NONBLOCK);

        devStructPtr->EvtStructList.q_forw = (struct qelem *)&devStructPtr->EvtStructList;
        devStructPtr->EvtStructList.q_back = (struct qelem *)&devStructPtr->EvtStructList;

        // set the first state
        devStructPtr->state           = inStateTablePtr[0].currentState;

        // set the first timer
        setDevTimer(devStructPtr, devStructPtr->stateTablePtr[0].secTimerVal, NULL);

        devStructPtr->rngId = rngCreate (MAX_RECV_BUF_SIZE);
        resetCmdLine(devStructPtr);

        // put the new device in the queue
        insque(devStructPtr, &devStructList);

        // if the caller wants the device pointer return it
        if (retDevStructPtr != NULL)
        {
            *retDevStructPtr = devStructPtr;
        }

    }
    return retStat;
}

void initPollFds()
{
  devStruct *devStructPtr;

  if (globalDevStructArray.devChangeFlag == 1)
  {

    bzero(&globalDevStructArray, sizeof(globalDevStructArray));

    for (devStructPtr  = getNextNode(NULL, allDevs);
         devStructPtr != NULL;
         devStructPtr  = getNextNode(devStructPtr, allDevs))
    {
      if (devStructPtr->devSetPolledPtr != NULL)
      {
        printf("adding %s to fds\n", devStructPtr->devName);
        devStructPtr->devSetPolledPtr(globalDevStructArray.fds,
                                      devStructPtr,
                                      &globalDevStructArray.activeDevCount);
      }
    }
    clearDevChangeFlag();
  }
}

void addToFds(devStruct *devStructPtr, int *idxPtr, int fd, short int mask)
{
  int idx = globalDevStructArray.activeDevCount;
  if (idx >= MAX_DEVICES)
  {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS, "addToFds fail\n");
      return;
  }

  if (fd != INVALID_FD)
  {
    globalDevStructArray.fds[idx].fd = fd;
    globalDevStructArray.fds[idx].events = mask;
    globalDevStructArray.devStructArray[idx] = devStructPtr;
    *idxPtr = idx;
    globalDevStructArray.activeDevCount += 1;
  }
}

int fdsCheck(int idx, int mask)
{
    int retStat;
    retStat = ((idx != NO_FDS_IDX) && (globalDevStructArray.fds[idx].revents & mask));
    return retStat;
}

// this routine will block until one of the devices in the fds
// becomes active.

int lastScanIdx = 0;
devStruct *getActiveDev()
{
  devStruct *devStructPtr = NULL;
  devStruct *retDevStructPtr = NULL;
  int pollStat;
  int idx;
  ReturnStatus retStat;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "waiting on poll\n");
  //prtFds();

  // wait for the device to become active.
  /*
   * if > 0 return is the number of entries in revents with non-zero status
         On success, a positive number is returned; this is the number of struc‐
       tures which have nonzero revents fields (in other words, those descrip‐
       tors  with events or errors reported).  A value of 0 indicates that the
       call timed out and no file descriptors were ready.   On  error,  -1  is
       returned, and errno is set appropriately.



   */
  pollStat = poll(globalDevStructArray.fds,
                  globalDevStructArray.activeDevCount,
                  -1);

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "pollStat %d\n", pollStat);

  if (pollStat <= 0)
  {
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "bad pollstat %d\n", pollStat);
  }
  else
  {
     for(idx=0;idx<MAX_DEVICES;idx++)
     {
        // need to do a round robin on the devices
        if (lastScanIdx >= MAX_DEVICES)
        {
            lastScanIdx = 0;
        }

         if (globalDevStructArray.fds[lastScanIdx].revents & POLLERR)
         {
             logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "POLLERR on fds lastScanIdx %d\n", lastScanIdx);
             setDevChangeFlag();
         }
         if (globalDevStructArray.fds[lastScanIdx].revents & POLLHUP)
         {
             logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "POLLHUP on fds lastScanIdx %d\n", lastScanIdx);
             setDevChangeFlag();
         }
         if (globalDevStructArray.fds[lastScanIdx].revents & POLLNVAL)
         {
             /* Invalid polling request.  */
             logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
                    "POLLNVAL on fds lastScanIdx %d ",
                    lastScanIdx);
             setDevChangeFlag();
             devStructPtr = globalDevStructArray.devStructArray[lastScanIdx];
	         if(devStructPtr != NULL)
	         {
	             // hopefully this debug info will help us find which device is misbehaving.
                 logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
                        "device %s\n",
                        devStructPtr->devName);
	         }
	         else
	         {
                 logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
                        "no devstruct for this fd\n");
	         }
         }

         // !!!SID!!! TBD - are there other events to handle in revent?

  	     devStructPtr = globalDevStructArray.devStructArray[lastScanIdx];
	     if(devStructPtr != NULL)
	     {
	       if (devStructPtr->devGetActivePtr != NULL)
	       {
             retStat = devStructPtr->devGetActivePtr(devStructPtr);
	         if (retStat == OK)
             {
		       retDevStructPtr = devStructPtr;
               lastScanIdx += 1;
		       break;
             }
           }
	     }
         lastScanIdx += 1;
       }//end for
  }
  return retDevStructPtr;
}

void prtFds(devStruct *outDevStructPtr)
{
  int idx;
  devStruct *devStructPtr;

  logmsg(outDevStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS, "fds struct");

  for(idx=0;idx<MAX_DEVICES;idx++)
  {
     devStructPtr = globalDevStructArray.devStructArray[idx];
     if (devStructPtr != NULL)
     {
         logmsg(outDevStructPtr->devFd, 0, LOG_LVL_ALWAYS,
                "%d. %p - ",idx, devStructPtr);
         if(devStructPtr == NULL)
         {
             logmsg(outDevStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS," ");
         }
         else
         {
             logmsg(outDevStructPtr->devFd,ADD_CR_FLAG, LOG_LVL_ALWAYS,
                    "%s", devStructPtr->devName);
             logmsg(outDevStructPtr->devFd,ADD_CR_FLAG, LOG_LVL_ALWAYS,
                    "%X", devStructPtr->devCookie);
         }
     }
  }
}

devStruct *getNextDev(devStruct *startDev)
{
    devStruct *ptr;

    // get the next device
    ptr = getNext(startDev, &devStructList);

    return ptr;
}


// find the next node in the list of the type specificed
devStruct *getNextNode(devStruct *startNode, devType_t devType)
{
    devStruct *ptr;


    for (ptr = getNextDev(startNode);
         ptr != NULL;
         ptr = getNextDev(ptr))
    {
      // find the type we're looking for
      if ((ptr->devType == devType) ||
          (devType == allDevs))
      {
          break;
      }
    }


    return ptr;
}

void setDevChangeFlag()
{
  globalDevStructArray.devChangeFlag = 1;
}
void clearDevChangeFlag()
{
  globalDevStructArray.devChangeFlag = 0;
}

/** \brief InitDevices - each device type must add code here to be initialized prior to
 *                       the main loop running.
 *
 * \param
 * \param
 * \return ReturnStatus -
 *
 */

ReturnStatus initDevices()
{
  ReturnStatus retStat;
  devStruct *ptr;
  stateTable_t *stateTblPtr;

  bzero(&globalDevStructArray, sizeof(globalDevStructArray));

  retStat = helloProtocolInit();
  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS, "helloProtocolInit retStat %d\n", retStat);
//  assert(retStat == OK);

  retStat = tcpConnSrvInitSock();
  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS, "tcpConnSrvInitSock retStat %d\n", retStat);
//  assert(retStat == OK);

  retStat = initSerial();
  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_ALWAYS, "initSerial retStat %d\n", retStat);
//  assert(retStat == OK);

  //  run through the global device list and if specified activate table timers
  //  This will only happen once as the timers are reoccurring.
  for (ptr = getNextNode(NULL, allDevs);
       ptr != NULL;
       ptr = getNextNode(ptr, allDevs))
  {
    // return OK if a table timer transition was specified in the state table associated with this device
    retStat = getTableTimerEntry(ptr, &stateTblPtr);
    if (retStat == OK)
    {
      setDevTableTimer(ptr, stateTblPtr->secTimerVal);
    }
  }

  return retStat;
}

#define ALL_EVENTS (POLLIN|POLLPRI|POLLERR|POLLHUP|POLLNVAL)
#define IN_EVENT POLLIN
#define CHECK_EVENT_MASK (POLLIN|POLLPRI|POLLERR|POLLHUP|POLLNVAL)

ReturnStatus devGenericSetPolled(struct pollfd *fds,
                                  devStruct *devStructPtr)
 {
    ReturnStatus retStat = OK;
    addToFds(devStructPtr,
             &devStructPtr->evtIdx,
             devStructPtr->evtFd,
             POLLIN);
    addToFds(devStructPtr,
             &devStructPtr->fdsTimerIdx,
             devStructPtr->timerFd,
             POLLIN);
    addToFds(devStructPtr,
             &devStructPtr->fdsTableTimerIdx,
             devStructPtr->tableTimerFd,
             POLLIN);
    addToFds(devStructPtr,
             &devStructPtr->fdsIdx,
             devStructPtr->devFd,
             CHECK_EVENT_MASK);
    return retStat;
}

ReturnStatus devGenericGetActive(struct devStruct *devStructPtr)
{

   ReturnStatus retStat = ERROR;

   if (fdsCheck(devStructPtr->evtIdx, POLLIN))
   {
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsTimerIdx, POLLIN))
   {
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsTableTimerIdx, POLLIN))
   {
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsIdx, CHECK_EVENT_MASK))
   {
      if (globalDevStructArray.fds[devStructPtr->fdsIdx].revents & POLLPRI)
      {
          pollPriEvtCnt += 1;
      }
      retStat = OK;
   }
   else
   {
     int idx = devStructPtr->fdsIdx;
     if (globalDevStructArray.fds[idx].revents == 0)
     {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
               "devGenericGetActive - no bits set in revent\n");
     }
     else
     {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
               "devGenericGetActive unhandled bits %lx\n",
               globalDevStructArray.fds[idx].revents);
     }
   }
   return retStat;
}

ReturnStatus devGenericGetEvtType(struct devStruct *devStructPtr,
                                  eventStruct *retEvtPtr)
{
   ReturnStatus retStat;

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
       free(evtPtr);
       retStat = OK;
    }
   else if (fdsCheck(devStructPtr->fdsTimerIdx, POLLIN))
   {
      uint64_t timebuf;

      read(devStructPtr->timerFd, &timebuf, sizeof(timebuf));
      retEvtPtr->eventType = evtTimerExpired;
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsTableTimerIdx, POLLIN))
   {
      uint64_t timebuf;

      read(devStructPtr->tableTimerFd, &timebuf, sizeof(timebuf));
      retEvtPtr->eventType = evtTableTimerExpired;
      retStat = OK;
   }
   else if (fdsCheck(devStructPtr->fdsIdx, CHECK_EVENT_MASK))
   {
      retEvtPtr->eventType = evtSocketInput;
      retStat = OK;
   }
   else
   {
       logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no hello event to get\n");
   }
   return retStat;
}
