ReturnStatus helloProtocolInit();
ReturnStatus getMyAddr(char *);
void prtNodeList(devStruct *devStructPtr);
ReturnStatus tcpCmdClientInit(struct sockaddr_in cliaddr);
ReturnStatus tcpCmdClientShutdown(devStruct *ptr, eventStruct *evtPtr);
