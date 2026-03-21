#include "listener_socket.h"
#include "socket.h"

ListenerSocket :: ListenerSocket(const char *socketName, SpawnFunction_t spawn, const char *spawnName)
{
    this->socketName = socketName;
    spawnFunction = spawn;
    this->spawnName = spawnName;
    port = 0;
    listenfd = -1;
    listenerTask = 0;
}

ListenerSocket :: ~ListenerSocket()
{
    if (listenfd >= 0) {
        closesocket(listenfd);
        vTaskDelay(10);
    }
    vTaskDelete(listenerTask);
}


void ListenerSocket :: task(void *a)
{
    ListenerSocket *obj = (ListenerSocket *)a;
    obj->ListenerTask();
}

void ListenerSocket :: ListenerTask()
{
    unsigned long int clilen;
    struct sockaddr_in cli_addr;

    clilen = sizeof(cli_addr);

    while(1) {
        // Here process will go in sleep mode and will wait for the incoming connection
        int newsockfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            printf("ERROR %s: Accept\n", socketName);
        } else {
            xTaskCreate(spawnFunction, spawnName, configMINIMAL_STACK_SIZE, (void *)newsockfd, PRIO_NETSERVICE, NULL);
        }
    }
}


int ListenerSocket :: Start(int port)
{
    struct sockaddr_in serv_addr;

    if (listenerTask) {
        vTaskDelete(listenerTask);
        listenerTask = 0;
    }

    if (listenfd >= 0) {
        closesocket(listenfd);
    }

    // initialize socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenfd < 0) {
        printf("ERROR %s: opening listening socket: Error: %d\n", socketName, listenfd);
        return -1;
    }

    /* Initialize socket structure */
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    /* Now bind the host address using bind() call.*/
    if (bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        printf("ERROR %s: binding\n", socketName);
        return -2;
    }

    // Now start listening for the clients
    listen(listenfd, 5);
    printf("INFO %s: listening\n", socketName);

    xTaskCreate( ListenerSocket :: task, socketName, configMINIMAL_STACK_SIZE, this, PRIO_NETSERVICE, &listenerTask );

    return 0;
}
