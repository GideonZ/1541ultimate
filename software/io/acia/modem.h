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

#define MODEM_NUM_REGS 16
#define MODEM_REG_AUTOANSWER  0
#define MODEM_REG_RINGCOUNTER 1
#define MODEM_REG_ESCAPE      2
#define MODEM_REG_ESCAPETIME  12

class Modem : public ConfigurableObject
{
    static void task(void *a);
    static void listenerTask(void *a);
    static void callerTask(void *a);
    void ModemTask(void);
    void IncomingConnection(int socket);
    void Caller(void);
    void CollectCommand(ModemCommand_t *cmd, char *buf, int len);
    bool ExecuteCommand(ModemCommand_t *cmd);
    void RunRelay(int socket);
    void ResetRegisters();
    void WriteRegister(int value);
    int  ReadRegister();
    void SetHandshakes(bool connected);
    void RelayFileToSocket(const char *filename, int socket, const char *alt);

    QueueHandle_t commandQueue;
    QueueHandle_t connectQueue;
    SemaphoreHandle_t connectionLock;
    QueueHandle_t aciaQueue;
    DataBuffer *aciaTxBuffer;
    ListenerSocket *listenerSocket;
    uint8_t ctsMode, dsrMode, dcdMode;
    uint8_t lastHandshake;
    bool keepConnection;
    bool commandMode;
    int baudRate;
    bool dropOnDTR;
    int registerSelect;
    uint8_t registerValues[MODEM_NUM_REGS];
public:
    Modem();
    void effectuate_settings();
};

extern Modem modem;

#endif
