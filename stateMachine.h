struct devStruct; //forward declaration

typedef ReturnStatus (*transRtn)(struct devStruct *, eventStruct *);

#define NO_STATE_CHANGE -1
#define WILDCARD_STATE -2
#define WILDCARD_EVENT -3
#define NO_TIMEOUT 0

#define SEQUENCE_IN_PROGRESS_FLAG (1 << 0)
#define SEQUENCE_COMPLETE_FLAG (1 << 1)
#define TABLE_TIMER_FLAG (1 << 2)

typedef struct
{
   devState_t   currentState;
   eventType_t  event;
   devState_t   nextState;
   devState_t   errState;
   transRtn     transRtnPtr;
   transRtn     errTransRtnPtr;
   ulong        secTimerVal;
   ulong        flags;
}stateTable_t;

ReturnStatus getTransRtn(struct devStruct *devStructPtr,
                         eventStruct *evtStructPtr,
                         transRtn *retTransRtnPtr,
                         stateTable_t **retStateTblPtr);

ReturnStatus getTableTimerEntry(devStruct *devStructPtr,
                                stateTable_t **retStateTblPtr);
