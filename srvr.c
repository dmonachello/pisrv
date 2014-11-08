/* Sample UDP server */

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
#include <assert.h>

#include "utils.h"
#include "srvr.h"
#include "device.h"
#include "helloProtocol.h"
#include "tcpConnSrv.h"
#include "serialCmdSrv.h"

int main(int argc, char**argv)
{
  ReturnStatus retStat;
  devStruct *devStructPtr;
  int loopCount;
  stateTable_t *stateTblEntryPtr;
  transRtn transRtnPtr = NULL;
  eventStruct *evtStructPtr = NULL;
  int remainingSeconds = 0;
  int newTimerValue;

  logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "start server\n");

  // call all the device init routines
  retStat = initDevices();

  // set the flag so we init the fds struct at the top of the main loop
  setDevChangeFlag();

  // loop forever
  for(loopCount=1;;loopCount++)
  {
    // if the change flag is set then make sure the fds is setup properly
    initPollFds();

    // debug code begin
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "---->loop count %d\n", loopCount);

    // debug code end

    // wait for a device to become active
    devStructPtr = getActiveDev();

    // make sure we got a device
    if (devStructPtr == NULL)
    {
      // TBD - find out why we unblocked in getActiveDev but didn't get an active device
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "no device ready\n");
    }
    else
    {
      evtStructPtr = calloc(1, sizeof(eventStruct));
      assert(evtStructPtr != NULL);

      // get the event that woke the device up
      retStat = devStructPtr->devGetEvtPtr(devStructPtr, evtStructPtr);

      // track it in case it isn't being freed
      evtStructPtr->eventSeqNum = loopCount;
      if (retStat == OK)
      {

        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "event = %d\n", evtStructPtr->eventType);

        // get the device specific trans routine for this event
        retStat = getTransRtn(devStructPtr, evtStructPtr, &transRtnPtr, &stateTblEntryPtr);

        // in debug code bomb here if we didn't find a transition
        // i.e. the state table is not right.
//        assert(retStat == OK);

        if (retStat == NO_STATE_TRANS_FOUND)
	    {
            logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no state trans found %s %d\n",
                   devStructPtr->devName, evtStructPtr->eventType);
	    }
	    else if (retStat == OK)
	    {
          // cancel active timer
          setDevTimer(devStructPtr, 0, &remainingSeconds);

          if (transRtnPtr == NULL)
          {
              logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "no trans routine");
          }
          else
          {
              // call the trans routine for this event
              retStat = transRtnPtr(devStructPtr, evtStructPtr);
          }

	      if (retStat == OK)
	      {
#if 0
            if (evtStructPtr->eventType == evtParseComplete)
            {
                // send the good response back
                logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
                       "%s OK",
                       devStructPtr->argPtrs[0]);
            }
#endif

            if (stateTblEntryPtr->nextState != NO_STATE_CHANGE)
            {
                logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
                       "state change dev %s from %d to %d\n",
                       devStructPtr->devName,
                       devStructPtr->state,
                       stateTblEntryPtr->nextState);

                // all went OK so trans to the next state
                devStructPtr->state = stateTblEntryPtr->nextState;
            }
          }
	      else if (retStat == DEVICE_SHUTDOWN)
	      {
	          devStructPtr = NULL;
	          continue;
	      }
	      else
	      {
	          // send back failure status
            logmsg(devStructPtr->devFd, ADD_CR_FLAG, LOG_LVL_ALWAYS,
                   "%s FAIL",
                   devStructPtr->argPtrs[0]);

            if (stateTblEntryPtr->errState != NO_STATE_CHANGE)
            {
                if (stateTblEntryPtr->errTransRtnPtr != NULL)
                {
                    // call error trans routine
                    retStat = stateTblEntryPtr->errTransRtnPtr(devStructPtr,
                                                               evtStructPtr);
                }
                // transition to the error state. yes I'm ignoring the
                // return status of the error routine.
                devStructPtr->state = stateTblEntryPtr->errState;
            }
          }


          if ((stateTblEntryPtr->nextState != NO_STATE_CHANGE) ||
              (remainingSeconds == 0))
          {
            newTimerValue = stateTblEntryPtr->secTimerVal;
          }
          else
          {
            newTimerValue = remainingSeconds;
          }
          // set active timer
          setDevTimer(devStructPtr, newTimerValue, NULL);

        }
      }

      if ((evtStructPtr->eventFlags & EVENT_FLAG_DONT_FREE) == 0)
      {
        // cleanup this transaction
        free(evtStructPtr);
      }
      else
      {
        logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL,
               "main - not freeing event %p %d\n",
               evtStructPtr, evtStructPtr->eventSeqNum);
      }
    }
  }//end for
}
