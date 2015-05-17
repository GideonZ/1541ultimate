#ifndef STACK_H
#define STACK_H

#ifndef ENTER_SAFE_SECTION
#include "itu.h"
#endif

template <class T>
class Stack
{
private:
    int capacity;
    int count;
    T *elements;
    T empty;
public:
    Stack(int size, T def) {
        empty = def;
        elements = new T[size];
        capacity = size;
        count = 0;
    }
    
    ~Stack() {
        delete[] elements;
    }
    
    int get_count(void) {
        return count;
    }
    
    bool is_empty(void) {
        return (count == 0);
    }

    T top(void) {
        ENTER_SAFE_SECTION
        T ret;
        if(!count) {
            ret = empty;
        } else {
            ret = elements[count-1];
        }
        LEAVE_SAFE_SECTION
        return ret;
    }
    
    T pop(void) {
        ENTER_SAFE_SECTION
        T ret;
        if(!count) {
            ret = empty;
        } else {
            --count;
            ret = elements[count];
        }
        LEAVE_SAFE_SECTION
        return ret;
    }
    
    bool push(T e) {
        ENTER_SAFE_SECTION
		bool ret = false;
		if(count != capacity) {
            elements[count++] = e;
            ret = true;
        }
        LEAVE_SAFE_SECTION
		return ret;
    }
};
#endif
