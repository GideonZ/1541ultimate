#pragma once
#include "FreeRTOS.h"
// Timers are registered but never scheduled by the scripted WiFi task.
typedef void *TimerHandle_t;
inline TimerHandle_t xTimerCreate(const char *, TickType_t, BaseType_t, void *id, void (*)(TimerHandle_t)) { return id; }
inline BaseType_t xTimerStart(TimerHandle_t, TickType_t) { return pdTRUE; }
inline void *pvTimerGetTimerID(TimerHandle_t timer) { return timer; }
