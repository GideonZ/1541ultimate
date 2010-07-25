#ifndef POLL_H
#define POLL_H

#include "event.h"
#include "indexed_list.h"

typedef void(*PollFunction)(Event&);

extern IndexedList<PollFunction> poll_list;

void nop(Event& e);

#endif
