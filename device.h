ReturnStatus createDevStruct(char *inDevName,
                             devType_t devType,
                             stateTable_t *inStateTablePtr,
                             devInit inDevInitPtr,
                             devGetActive inDevGetActivePtr,
                             devSetPolled inDevSetPolledPtr,
                             devGetEvt inDevGetEvtPtr,
                             devStruct **retDevStructPtr);
ReturnStatus initDevices();
devStruct *getActiveDev();
devStruct *getNextDev(devStruct *startDev);
devStruct *getNextNode(devStruct *startNode, devType_t devType);

void initPollFds();
void addToFds(devStruct *devStructPtr, int *idxPtr, int fd, short int mask);

ReturnStatus devGenericSetPolled(struct pollfd *fds,
                                  devStruct *devStructPtr);
ReturnStatus devGenericGetEvtType(struct devStruct *devStructPtr,
                                  eventStruct *retEvtPtr);
ReturnStatus devGenericGetActive(struct devStruct *devStructPtr);

void prtFds(devStruct *);
int fdsCheck(int idx, int mask);

void setDevChangeFlag();
void clearDevChangeFlag();
void prtDevQueLen();

