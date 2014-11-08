#ifndef MSGAPI_H_INCLUDED
#define MSGAPI_H_INCLUDED

ReturnStatus msgSendAll(void *buffer, int len, int flags);

#define MSG_API_UNINITIALED -20

#endif // MSGAPI_H_INCLUDED
