#ifndef MANAGED_ARRAY_H
#define MANAGED_ARRAY_H

#include "integer.h"

#ifndef ENTER_SAFE_SECTION
#include "itu.h"
#endif

template <class T>
class ManagedArray
{
private:
	T *element_array;
	T empty;
	int size;
	void expand(int newsize) {
		ENTER_SAFE_SECTION
		T *new_array;
		new_array = new T[newsize];
		for(int i=0;i<size;i++) {
			new_array[i] = element_array[i];
		}
		if(element_array)
			delete element_array;
		element_array = new_array;
		size = newsize;
		LEAVE_SAFE_SECTION
	}
public:
    ManagedArray(int initial, T def) : empty(def) {
		if(initial) {
			element_array = new T[initial];
		} else {
			element_array = NULL;
		}
		size = initial;
    }
    
    ~ManagedArray() {
		if(element_array)
			delete element_array;
    }
    
	void set(int i, T el) {
		if(i < 0)
			return;
		if(i >= size) {
			expand(i+1);
		}
		element_array[i] = el;
	}

	void unset(int i) {
		if(i < 0)
			return;
		if(i >= size) {
			return;
		}
		element_array[i] = empty;
	}

	int get_size(void) {
		return size;
	}

	T operator[] (int i) {
		if(i < 0)
			return empty;
		if(i >= size)
			return empty;
		return element_array[i];
	}
};
#endif
