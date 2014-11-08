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
#include "msgApi.h"

ReturnStatus msgApiSendAllMsgRtn(devStruct *, eventStruct *);
ReturnStatus msgApiSendErrMsgRtn(devStruct *, eventStruct *);
ReturnStatus msgApiCompleteOkMsgRtn(devStruct *, eventStruct *);
ReturnStatus msgApiCompleteErrMsgRtn(devStruct *, eventStruct *);
ReturnStatus msgApiCompleteMsgRtn(devStruct *, eventStruct *);
ReturnStatus msgApiResetRtn(devStruct *, eventStruct *);
ReturnStatus msgApiErrStateHandler(devStruct *, eventStruct *);
ReturnStatus msgApiNoopRtn(devStruct *, eventStruct *);


stateTable_t msgApiStateTable[] =
{
   {stateNodeActive,            //
    evtmsgSendAll,              // API called to send a msg to all nodes
    stateSendingAllMsgs,        //
    stateErr,                   // error state change
    &msgApiSendAllMsgRtn,       //
    &msgApiSendErrMsgRtn,       // error handler
    10,                         // 10 second timeout
    SEQUENCE_IN_PROGRESS_FLAG},

   {stateSendingAllMsgs,        //
    evtAllSendOK,               // all msgs were sent ok
    statePendResponse,          //
    stateErr,                   //
    &msgApiCompleteOkMsgRtn,    //
    &msgApiCompleteErrMsgRtn,   // error handler
    10,                         // 10 second timeout in pend response state
    SEQUENCE_IN_PROGRESS_FLAG},

   {stateSendingAllMsgs,        //
    evtSendErr,                 // socket send error
    stateErr,                   //
    NO_STATE_CHANGE,            // no error state change
    &msgApiCompleteErrMsgRtn,   //
    NULL,                       // no error handler
    NO_TIMEOUT,                 // no timeout
    SEQUENCE_COMPLETE_FLAG},

/*
 * statePendResponse - waiting for responses from all other nodesm
 */
    {statePendResponse,          //
    evtAllRspOK,                // we have rcvd an ok rsp for all msgs
    stateOk,                    //
    NO_STATE_CHANGE,            // no error state change
    &msgApiCompleteMsgRtn,      //
    NULL,                       // no error handler
    NO_TIMEOUT,                 // no timeout
    SEQUENCE_COMPLETE_FLAG},

   {statePendResponse,          //
    evtOneMsgOK,                // one ok rsp received
    NO_STATE_CHANGE,            //
    NO_STATE_CHANGE,            // no error state change
    &msgApiCompleteMsgRtn,      //
    NULL,                       // no error handler
    10,                         // 10 second timeout
    SEQUENCE_IN_PROGRESS_FLAG},

   {statePendResponse,           //
    evtOneMsgBad,           // a bad rsp received
    stateErr,               //
    NO_STATE_CHANGE,        // no error state change
    &msgApiCompleteMsgRtn,  //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    SEQUENCE_COMPLETE_FLAG},

   {stateErr,               //
    evtRetrived,            // retrived the final return status
    stateNodeActive,        //
    NO_STATE_CHANGE,        // no error state change
    &msgApiResetRtn,        //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    SEQUENCE_COMPLETE_FLAG},

   {stateOk,                //
    evtRetrived,            // retrived the final return status
    stateNodeActive,        //
    NO_STATE_CHANGE,        // no error state change
    &msgApiResetRtn,        //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    SEQUENCE_COMPLETE_FLAG},


   {WILDCARD_STATE,         //
    evtTimerExpired,        // timer expired
    stateErr,               //
    NO_STATE_CHANGE,        // no error state change
    &msgApiErrStateHandler, //
    NULL,                   // no error handler
    NO_TIMEOUT,             // no timeout
    SEQUENCE_COMPLETE_FLAG},

   {WILDCARD_STATE,         //
    WILDCARD_EVENT,         // timer expired
    NO_STATE_CHANGE,        //
    NO_STATE_CHANGE,        // no error state change
    &msgApiNoopRtn,         //
    NULL,                   // no error handler
    10,                     // 10 second timeout - !!!SID!!! is this right?
    SEQUENCE_IN_PROGRESS_FLAG},

   {stateLastState}
};


devStruct *msgApiDevPtr = NULL;

/*
 * API routines
 */

ReturnStatus msgSendAll(void *buffer, int len, int flags)
{
    ReturnStatus retStat  = MSG_API_UNINITIALED;

    if (msgApiDevPtr != NULL)
    {

        retStat = evtAdd(evtmsgSendAll, msgApiDevPtr, buffer, len);
    }

    return retStat;
}


/*
 * state transition routines
 */

/*
 * transition routine to send a message to all known nodes
 */
ReturnStatus msgApiSendAllMsgRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    devStruct *ptr;
    ReturnStatus retStat = OK;
    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiSendAllMsgRtn\n");

    for (ptr = getNextNode(NULL, tcpCmdClient);
         ptr != NULL;
         ptr = getNextNode(ptr, tcpCmdClient))
    {
        retStat = send(ptr->devFd,
                       &evtPtr->eventData,
                       evtPtr->eventDataLen,
                       0);
        if (retStat <= 0)
        {
            retStat = ERROR;
            break;
        }
    }// end for

    // if NULL then we sent them all OK
    if (ptr == NULL)
    {
        retStat = evtAdd(evtAllSendOK, msgApiDevPtr, NULL, 0);
    }

    return retStat;
}

ReturnStatus msgApiSendErrMsgRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiSendErrMsgRtn\n");

    return retStat;
}

ReturnStatus msgApiCompleteOkMsgRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiCompleteOkMsgRtn\n");

    return retStat;
}

ReturnStatus msgApiCompleteErrMsgRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiCompleteErrMsgRtn\n");

    return retStat;
}

ReturnStatus msgApiCompleteMsgRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiCompleteMsgRtn\n");

    return retStat;
}

ReturnStatus msgApiResetRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiResetRtn\n");

    return retStat;
}

ReturnStatus msgApiErrStateHandler(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiErrStateHandler\n");

    return retStat;
}

ReturnStatus msgApiNoopRtn(devStruct *devStructPtr, eventStruct *evtPtr)
{
    ReturnStatus retStat = OK;

    logmsg(0, USE_STDOUT_FLAG, LOG_LVL_NORMAL, "msgApiNoopRtn\n");

    return retStat;
}

