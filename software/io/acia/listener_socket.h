#ifndef LISTENER_SOCKET
#define LISTENER_SOCKET

#include "FreeRTOS.h"
#include "task.h"

typedef void(*SpawnFunction_t)(void *obj);

class ListenerSocket
{
    TaskHandle_t listenerTask;
    int port;
    int listenfd;

    const char *socketName;
    const char *spawnName;
    SpawnFunction_t spawnFunction;

    static void task(void *a);
    void ListenerTask(void);
public:
    ListenerSocket(const char *socketname, SpawnFunction_t func, const char *spawnName);
    virtual ~ListenerSocket();

    int Start(int portNr);
};



#endif // LISTENER_SOCKET
