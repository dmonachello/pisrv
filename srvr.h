#include "rnglib.h"

typedef int ReturnStatus;

#define PISRV_PORT_NUM 32000
#if 0
typedef struct
{
  queElem hdr;
  struct sockaddr_in address;

  int tcpCmdSrvFd;
  int tcpServerFd;

  int pingCount;
  int activeCount;
}nodes;
#endif

typedef enum
{
   evtUnknown = 0,

   evtSocketInput,
   evtSocketShutdown,

   evtSerialInput,

   evtTimerExpired,
   evtTableTimerExpired,
   evtTimerSet,
   evtQueued,

   evtParseComplete,
   evtParseCompletePlus,
   evtParseNotComplete,
   evtAssembleComplete,
   evtAssembleCompletePlus,
   evtCmdFinished,
   evtRemoteCmdStart,

   evtmsgSendAll,
   evtmsgSendOne,
   evtAllSendOK,
   evtSendErr,
   evtAllRspOK,
   evtOneMsgOK,
   evtOneMsgBad,
   evtRetrived,

   evtLast
}eventType_t;

#define MAX_EVENT_CHAR_DATA_LEN 128
typedef union
{
  ulong longIntData;
  char  charArrayData[MAX_EVENT_CHAR_DATA_LEN];
}eventData_t;

#define EVENT_FLAG_DONT_FREE 1
typedef struct
{
  queElem hdr;

  eventType_t   eventType;
  int           eventDataLen;
  ulong         eventFlags;
  ulong         eventSeqNum;
  eventData_t   eventData;
}eventStruct;

typedef enum
{
   stateNoState = 0,

   stateProtocolActive,
   stateNodeActive,

   stateSendingAllMsgs,
   statePendResponse,
   stateErr,
   stateOk,


   stateCmdWait,
   stateCmdAssemble,
   stateCmdParse,
   stateCmdExec,
   stateCmdExecPlus,


   stateLastState
}devState_t;

typedef enum
{
    noDev = 0,

    tcpConnSrv,
    tcpCmdSrv,
    hello,
    serial,
    tcpCmdClient,

    allDevs,

    endDev
}devType_t;

struct devStruct; //forward declaration

#include "stateMachine.h"


typedef ReturnStatus (*devInit)(struct devStruct *);
typedef ReturnStatus (*devGetActive)(struct devStruct *);
typedef void (*devSetPolled)(struct pollfd *, devStruct *, int *);
typedef ReturnStatus (*devGetEvt)(struct devStruct *, eventStruct *);

#define MAX_DEV_NAME 32
#define MAX_RECV_BUF_SIZE 132
#define MAX_ARGS 10

typedef unsigned long devCookie_t;

#define DEV_COOKIE_LIVE 0xbeadba11
#define DEV_COOKIE_DEAD 0xdeadbeef
typedef struct devStruct
{
  queElem      hdr;

  devCookie_t   devCookie;
  char         devName[MAX_DEV_NAME];
  devType_t    devType;
  devState_t   state;
  int          devFd;
  int          evtFd;
  int          timerFd;
  int          tableTimerFd;

  queElem      EvtStructList;

  stateTable_t *stateTablePtr;

  devInit      devInitPtr;
  devGetActive devGetActivePtr;
  devSetPolled devSetPolledPtr;
  devGetEvt    devGetEvtPtr;

  int fdsIdx;
  int fdsTimerIdx;
  int fdsTableTimerIdx;
  int evtIdx;

  int  bufIdx;
  char *bufPtr;
  char *nextArg;
  char *savePtr;
  char recvBuf[MAX_RECV_BUF_SIZE];
  int argCount;
  char *argPtrs[MAX_ARGS];
  RING_ID rngId;


  //node struct moved here
//  queElem hdr;
  struct sockaddr_in address;

//  int tcpCmdSrvFd;
//  int tcpServerFd;

  int pingCount;
  int activeCount;

}devStruct;


#define OK 0
#define ERROR -1
#define NO_STATE_TRANS_FOUND -2
#define DEVICE_SHUTDOWN -3


#define NO_TIMER         -1
#define INVALID_FD       -1
#define MAX_DEVICES      32
#define MAX_DEV_NAME_LEN 32

typedef struct
{
  struct pollfd fds[MAX_DEVICES];
  int activeDevCount;
  int devChangeFlag;
  devStruct *devStructArray[MAX_DEVICES];
}globalDeviceStruct;

#if 0
#define UDP_HELLO_FD 0
#define UDP_HELLO_TIMER_FD 1
#define TCP_SRV_FD 2
#endif

#define HELLO_NODE_LIMIT 5
#define NO_FDS_IDX -1

ReturnStatus evtAdd(eventType_t evtType, devStruct *devStructPtr, void *data, int dataSize);
eventStruct *evtGet(devStruct *devStructPtr);
