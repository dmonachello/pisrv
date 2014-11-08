#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <poll.h>
#include <sys/timerfd.h>
#include <time.h>

#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <netinet/in.h>

#include "utils.h"
#include "srvr.h"

char buffer[128];
int logLvl_g = LOG_LVL_DBG;

void setLogLvl(char *lvl)
{
    logLvl_g = atoi(lvl);
}
// the incoming log level needs to >= to the global level
// i.e. inLogLvl of LOG_LVL_DBG won't print if global level
// is LOG_LVL_NORMAL
int logmsg(int outFd, int flags, int inLogLvl, char *fmt, ...)
{
    va_list ap;
    int wrStat = 0;

    // if the incoming msg is equal or of more important than the global log level
    //  then print it.
    if (inLogLvl >= logLvl_g)
    {
        va_start(ap, fmt);

        vsnprintf(buffer, sizeof(buffer), fmt, ap);

        if (flags & USE_STDOUT_FLAG)
        {
            puts(buffer);
            if (flags & ADD_CR_FLAG)
            {
                puts("\r\n");
            }
        }
        else
        {
            wrStat = write(outFd, buffer, strlen(buffer));
            if (flags & ADD_CR_FLAG)
            {
                write(outFd, "\r\n",2);
            }
        }

        va_end(ap);
    }
    return wrStat;
}


void *getNext(void *entry, void *header)
{
  struct qelem *ptr;
  struct qelem *listHead = header;

  if (entry == NULL)
  {
    ptr = listHead;
  }
  else
  {
    ptr = entry;
  }

  ptr = ptr->q_forw;

  if (ptr == listHead)
  {
    ptr = NULL;
  }

  return ptr;
}

void oldinsque(void *entry, void *header)
{
  struct qelem *e = (struct qelem *) entry;
  struct qelem *p = (struct qelem *) header;

  e->q_forw = p->q_forw;
  e->q_back = p;
  p->q_forw->q_back = e;
  p->q_forw = e;
}
void  insque(void *entry, void *header)
{
  struct qelem *elem = (struct qelem *) entry;
  struct qelem *first = (struct qelem *) header;

  elem->q_forw = first;
  elem->q_back = first->q_back;

  first->q_back->q_forw = elem;
  first->q_back = elem;
}

void remque (void *e)
{
  struct qelem *elem = (struct qelem *)e;
  elem -> q_forw -> q_back = elem -> q_back;
  elem -> q_back -> q_forw = elem -> q_forw;
}

int queLen (void *lh)
{
  int retCnt = 0;
  struct qelem *ptr;
  struct qelem *listHead = lh;

  for (ptr = listHead->q_forw; ptr != listHead; ptr = ptr->q_forw)
  {
    retCnt += 1;
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "%p\n", ptr);
  }
  return retCnt;
}


ReturnStatus setDevTableTimer(devStruct *devStructPtr, int secTimerVal)
{
  struct itimerspec timeValue;
  ReturnStatus retStat = ERROR;
  int flags;

  if (devStructPtr->tableTimerFd == INVALID_FD)
  {
    devStructPtr->tableTimerFd = timerfd_create(CLOCK_REALTIME, 0);
    if (devStructPtr->tableTimerFd == ERROR)
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "timerfd_create");
      return ERROR;
    }

    // make the timer fd nonblocking
    flags = fcntl(devStructPtr->tableTimerFd , F_GETFL, 0);
    retStat = fcntl(devStructPtr->tableTimerFd , F_SETFL, flags | O_NONBLOCK);
  }

  if (secTimerVal == NO_TIMER)
  {
     logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no timer\n");
     return OK;
  }

  //if interval is nonzero and value is 0 this is a recurring timer?
  timeValue.it_interval.tv_sec = secTimerVal;
  timeValue.it_interval.tv_nsec = 0;
  timeValue.it_value.tv_sec = secTimerVal;
  timeValue.it_value.tv_nsec = 0;

  if (timerfd_settime(devStructPtr->tableTimerFd, 0,
                      &timeValue, NULL) == ERROR)
  {
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "error timerfd_settime");
    close(devStructPtr->tableTimerFd);
    devStructPtr->tableTimerFd = INVALID_FD;
  }
  else
  {
     retStat = OK;
  }

  return retStat;
}

// creates, activates or deactivates the devices timer.
// secTimerVal = 0 cancels the timer all other values activate the timer.
//
// TBD - some cleanup
//      - why are we setting the interval and the value fields of the timer?
//      - does nonblocking work?
ReturnStatus setDevTimer(devStruct *devStructPtr, int secTimerVal, int *remainingSeconds)
{
  struct itimerspec timeValue;
  struct itimerspec retTimeValue;
  ReturnStatus retStat = ERROR;
  int flags;

  if (devStructPtr->timerFd == INVALID_FD)
  {
    devStructPtr->timerFd = timerfd_create(CLOCK_REALTIME, 0);
    if (devStructPtr->timerFd == ERROR)
    {
      logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "timerfd_create");
      return ERROR;
    }

    // make the timer fd nonblocking
    flags = fcntl(devStructPtr->timerFd , F_GETFL, 0);
    retStat = fcntl(devStructPtr->timerFd , F_SETFL, flags | O_NONBLOCK);
  }

  if (secTimerVal == NO_TIMER)
  {
     logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "no timer\n");
     return OK;
  }

  //if interval is 0 and value is nonzero this is a one-shot timer
  timeValue.it_interval.tv_sec = 0;
  timeValue.it_interval.tv_nsec = 0;
  timeValue.it_value.tv_sec = secTimerVal;
  timeValue.it_value.tv_nsec = 0;

  if (timerfd_settime(devStructPtr->timerFd, 0,
                      &timeValue, &retTimeValue) == ERROR)
  {
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG, "error timerfd_settime");
    close(devStructPtr->timerFd);
    devStructPtr->timerFd = INVALID_FD;
    retStat = ERROR;
  }
  else
  {
     retStat = OK;
     if (remainingSeconds != NULL)
     {
         *remainingSeconds = timeValue.it_value.tv_sec;
         if (*remainingSeconds != 0)
         {
             logmsg(0, USE_STDOUT_FLAG, LOG_LVL_DBG,
                    "time remaining %d\n", *remainingSeconds);
         }
     }
  }

  return retStat;
}

ReturnStatus evtAdd(eventType_t evtType, devStruct *devStructPtr, void *data, int dataSize)
{
    ReturnStatus retStat = OK;
    eventStruct *evtPtr;

    evtPtr = calloc(1, sizeof(eventStruct));
    if (evtPtr == NULL)
    {
        retStat = ERROR;
    }
    else
    {
        uint64_t cnt = 1;

        evtPtr->eventType = evtType;
        if (dataSize > 0)
        {
            bcopy(data, &evtPtr->eventData, dataSize);
        }
        insque(evtPtr, &devStructPtr->EvtStructList);

        //set event fd
        retStat = write(devStructPtr->evtFd,
                        &cnt,
                        sizeof(cnt));
        if (retStat > 0)
        {
            retStat = OK;
        }
    }
    return retStat;
}

eventStruct *evtGet(devStruct *devStructPtr)
{
    eventStruct *ptr = NULL;
    uint64_t cnt;
    ReturnStatus retStat = OK;

    ptr = getNext(NULL, &devStructPtr->EvtStructList);
    if (ptr != NULL)
    {
        remque(ptr);
        retStat = read(devStructPtr->evtFd,
                       &cnt,
                       sizeof(cnt));
        if (retStat <= 0)
        {
            // !!!SID!!! TBD - what do we do with this?
            ;
        }
    }

    return ptr;
}

