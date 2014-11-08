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


ReturnStatus getTableTimerEntry(devStruct *devStructPtr,
                                stateTable_t **retStateTblPtr)
{
   int idx;
   ReturnStatus retStat = NO_STATE_TRANS_FOUND;
   stateTable_t *stateTbl = devStructPtr->stateTablePtr;

   for(idx=0; stateTbl[idx].currentState != stateLastState; idx++)
   {
      if ((stateTbl[idx].flags & TABLE_TIMER_FLAG) != 0)
	  {
	    *retStateTblPtr = &stateTbl[idx];
	    retStat = OK;
	    break;
	  }
   }
   return retStat;
}

ReturnStatus getTransRtn(devStruct *devStructPtr,
                         eventStruct *evtStructPtr,
                         transRtn *retTransRtnPtr,
                         stateTable_t **retStateTblPtr)
{
   int idx;
   ReturnStatus retStat = NO_STATE_TRANS_FOUND;
   stateTable_t *stateTbl = devStructPtr->stateTablePtr;

   for(idx=0; stateTbl[idx].currentState != stateLastState; idx++)
   {
      if (((devStructPtr->state       == stateTbl[idx].currentState) &&
           (evtStructPtr->eventType   == stateTbl[idx].event))       ||
          //this will match the 1st wildcard state it hits. is that right?
          (stateTbl[idx].currentState == WILDCARD_STATE)             ||
          (stateTbl[idx].event        == WILDCARD_EVENT))
	  {
	    *retTransRtnPtr = stateTbl[idx].transRtnPtr;
	    *retStateTblPtr = &stateTbl[idx];
	    retStat = OK;
	    break;
	  }
   }
   return retStat;
}
