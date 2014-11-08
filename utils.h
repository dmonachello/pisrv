
struct qelem {
  struct qelem *q_forw;
  struct qelem *q_back;
};

#define LOG_LVL_NONE 0
#define LOG_LVL_DBG 1
#define LOG_LVL_NORMAL 2
#define LOG_LVL_ERROR 4
#define LOG_LVL_ALWAYS 5


typedef struct qelem queElem;

void insque (void *elem, void *pred);
int queLen  (void *lh);
void remque (void *e);
void *getNext(void *entry, void *header);

#define USE_STDOUT_FLAG 1
#define ADD_CR_FLAG 2

int logmsg(int outFd, int flags, int logLvl, char *fmt, ...);

struct devStruct;
typedef struct devStruct devStruct;
typedef int ReturnStatus;
ReturnStatus setDevTimer(devStruct *devStructPtr, int secTimerVal, int *remainingSeconds);
ReturnStatus setDevTableTimer(devStruct *devStructPtr, int secTimerVal);
void setLogLvl(char *lvl);
