#ifndef FIFO_H
#define FIFO_H

#ifndef ENTER_SAFE_SECTION
#include "itu.h"
#endif

template <class T>
class Fifo
{
private:
    int read_pointer;
    int write_pointer;
    int capacity;
    int count;
    T *elements;
    T empty;
public:
    Fifo(int size, T def) {
        empty = def;
        elements = new T[size];
        write_pointer = 0;
        read_pointer = 0;
        count = 0;
        capacity = size;
    }
    
    ~Fifo() {
        delete[] elements;
    }
    
    int get_count(void) {
        return count;
    }
    
    bool is_empty(void) {
        return (count == 0);
    }

    T head(void) {
        ENTER_SAFE_SECTION
        T ret;
        if(!count) {
            ret = empty;
        } else {
            ret = elements[read_pointer];
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
            ret = elements[read_pointer++];
            if(read_pointer >= capacity)
                read_pointer = 0;
        }
        LEAVE_SAFE_SECTION
        return ret;
    }
    
    void push(T e) {
        ENTER_SAFE_SECTION
        if(count != capacity) {
            ++count;
            elements[write_pointer++] = e;
            if(write_pointer >= capacity)
                write_pointer = 0;
        }
        LEAVE_SAFE_SECTION
    }
};
#endif
