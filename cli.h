#ifndef CLI_H_INCLUDED
#define CLI_H_INCLUDED

typedef enum
{
    cmdNone = 0,

    cmdTest,
    cmdPrtDevs,
    cmdHelp,
    cmdPrtFds,
    cmdPrtNodes,
    cmdSetLogLvl,
    cmdPingReq,
    cmdPrtStats,
    cmdRemCmd,

    cmdLast
}cmdEnum_t;

typedef struct
{
    cmdEnum_t cmdNum;
    char *cmdStr;
    int maxArgs;
}cliDef_t;

ReturnStatus assembleCmdLine(devStruct *devStructPtr, int readLen);
ReturnStatus cliExeCmd(devStruct *, eventStruct *evtPtr);
ReturnStatus cliCleanupCmd(devStruct *, eventStruct *);
ReturnStatus cliParseRemaingBuffer(devStruct *, eventStruct *);
void resetCmdLine(devStruct *devStructPtr);
#endif // CLI_H_INCLUDED
