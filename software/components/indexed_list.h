#ifndef INDEXED_LIST_H
#define INDEXED_LIST_H

#include "integer.h"

#ifndef ENTER_SAFE_SECTION
#include "itu.h"
#endif

template <class T>
class IndexedList
{
private:
	T *element_array;
	T empty;
	BYTE *removal;
	int size;
	int elements;
	void expand(void) {
		T *new_array;
		BYTE *new_removal;
		if(!size)
			size = 16;
		else
			size *= 2;
		new_array = new T[size];
		new_removal = new BYTE[size];
		for(int i=0;i<elements;i++) {
			new_array[i] = element_array[i];
			new_removal[i] = removal[i];
		}
		if(element_array)
			delete element_array;
		if(removal)
			delete removal;
		element_array = new_array;
		removal = new_removal;
	}
public:
    IndexedList(int initial, T def) : empty(def) {
		if(initial) {
			element_array = new T[initial];
			removal = new BYTE[initial];
		} else {
			element_array = NULL;
			removal = NULL;
		}
			
		size = initial;
		elements = 0;
    }
    
    ~IndexedList() {
		if(element_array)
			delete element_array;
		if(removal)
			delete removal;
    }
    
	bool is_empty(void) {
		return (elements == 0);
	}

	void clear_list(void) {
		elements = 0;
	}

	void mark_for_removal(int idx) {
		if(idx < size)
			removal[idx] = 1;
	}
	
	void purge_list(void) {
		int left_over = 0;
		ENTER_SAFE_SECTION
		for(int i=0;i<elements;i++) {
			if(removal[i] == 0) {
				removal[left_over] = 0;
				if(i != left_over) {
					element_array[left_over] = element_array[i];
				}
				left_over ++;
			}
		}
		elements = left_over;
        LEAVE_SAFE_SECTION
	}

	void append(T el) {
//		printf("append");
		ENTER_SAFE_SECTION
		if(elements == size)
			expand();
		element_array[elements] = el;
		removal[elements] = 0;
		elements++;
        LEAVE_SAFE_SECTION
//		printf("ed. El=%d. Size=%d\n", elements, size);
	}
	
	int remove(T el) {
		ENTER_SAFE_SECTION
		int res = 0;
		for(int i=0;i<elements;i++) {
			if(element_array[i] == el) {
				elements--;
				for(int j=i;j<elements;j++) {
					element_array[j] = element_array[j+1];
					removal[j] = removal[j+1];
				}
				res = 1;
				break;
			}
		}
        LEAVE_SAFE_SECTION
        return res;
	}

	void swap(int i, int j) {
        ENTER_SAFE_SECTION
		T temp;
		temp = element_array[j];
		element_array[j] = element_array[i];
		element_array[i] = temp;
		BYTE temp2;
		temp2 = removal[j];
		removal[j] = removal[i];
		removal[i] = temp2;
        LEAVE_SAFE_SECTION
	}

	void replace(int i, T el) {
		element_array[i] = el;
	}
	
	void replace(T el, T el2) {
		for(int i=0;i<elements;i++) {
			if(element_array[i] == el) {
				element_array[i] = el2;
				break;
			}
		}
	}

	int get_elements(void) {
		return elements;
	}

	int get_size(void) {
		return size;
	}

	T operator[] (int i) {
		if(i < 0)
			return empty;
		if(i >= elements)
			return empty;
		return element_array[i];
	}

    void sort(int (*compare)(IndexedList<T> *, int, int)) {
		int swaps;
	    int i;
	    int gap;

		if(elements < 2)
			return;

	    gap = elements-1;

/*
		printf("Sorting:\n");
		for(int i=0;i<elements;i++) {
			printf("%d: %p\n", i, children[i]);
		}
*/
	    do {
	        //update the gap value for a next comb
	        if (gap > 1) {
	            gap = (10 * gap)/13;
	            if ((gap == 10) || (gap == 9))
	                gap = 11;
	        }

	        for(i=0,swaps=0;(i+gap)<elements;i++) {
	            if (compare(this, i, i+gap) > 0) {
	                swap(i, i+gap);
	                swaps++;
	            }
	        }
	    } while(swaps || (gap > 1));
	}
};
#endif
