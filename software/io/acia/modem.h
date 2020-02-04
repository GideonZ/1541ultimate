#ifndef MODEM_H
#define MODEM_H

#include "menu.h"
#include "acia.h"
#include "config.h"
#include "listener_socket.h"
#include "semphr.h"

#define MAX_MODEM_COMMAND_LENGTH 63
typedef struct {
    int length;
    int state;
    char command[MAX_MODEM_COMMAND_LENGTH+1];
} ModemCommand_t;

class Modem : public ConfigurableObject
{
    static void task(void *a);
    static void listenerTask(void *a);
    static void callerTask(void *a);
    void ModemTask(void);
    void IncomingConnection(int socket);
    void Caller(void);
    void CollectCommand(ModemCommand_t *cmd, char *buf, int len);
    void ExecuteCommand(ModemCommand_t *cmd);
    void RunRelay(int socket);

    QueueHandle_t commandQueue;
    QueueHandle_t connectQueue;
    SemaphoreHandle_t connectionLock;
    QueueHandle_t aciaQueue;
    DataBuffer *aciaTxBuffer;
    ListenerSocket *listenerSocket;
    bool connected;
public:
    Modem();
    void effectuate_settings();
};

extern Modem modem;

#endif
