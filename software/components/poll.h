#ifndef POLL_H
#define POLL_H

#include "event.h"
#include "indexed_list.h"

extern "C" void main_loop(void *a);

typedef void(*PollFunction)(Event&);

void nop(Event& e);

class MainLoop
{
	static IndexedList<PollFunction> *getPollList() {
		static IndexedList<PollFunction> poll_list(24, 0);
		return &poll_list;
	}

public:
	static void run(void *a);
	static void nop(void);

	static void addPollFunction(PollFunction f) {
		getPollList() -> append(f);
	}

	static void removePollFunction(PollFunction f) {
		getPollList() -> remove(f);
	}
};

#endif
