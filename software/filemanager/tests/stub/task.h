#ifndef SDPOLL_TASK_H
#define SDPOLL_TASK_H
typedef void *TaskHandle_t;
#define configMINIMAL_STACK_SIZE 1600
#define PRIO_BACKGROUND 1
static inline int xTaskCreate(void (*)(void *), const char *, int, void *, int, TaskHandle_t *h) { if (h) *h = 0; return 1; }
static inline void vTaskDelay(int) { }
static inline void vTaskDelete(TaskHandle_t) { }
#endif
