#ifndef DICT_H
#define DICT_H

#include <stdint.h>
#include "indexed_list.h"

template <class K, class V>
class Dict
{
private:
    IndexedList<K> keys;
    IndexedList<V> values;
    K defK;
    V defV;
    int (*compare)(K, K);
public:
    Dict(int initial, K defK, V defV, int(*compfunc)(K, K)) : keys(initial, defK), values(initial, defV), compare(compfunc)
    {
        this->defK = defK;
        this->defV = defV;
    }
    
	bool is_empty(void) {
		return keys.is_empty();
	}

	void clear(void) {
        keys.clear_list();
        values.clear_list();
	}

	void append(K key, V val) {
        keys.append(key);
        values.append(val);
	}
	
	int pop(K key) {
        int i = index_of(key);
        if (i != -1) {
            keys.remove(i);
            values.remove(i);
        }
        return i;
	}

	void swap(int i, int j) {
        keys.swap(i, j);
        values.swap(i, j);
	}

	int get_elements(void) {
		return keys.get_elements();
	}

	int get_size(void) {
		return keys.get_size();
	}

    K get_key(int index) {
        if (index < 0 || index >= keys.get_elements()) {
            return defK;
        }
        return keys[index];
    }

    V get_value(int index) {
        if (index < 0 || index >= values.get_elements()) {
            return defV;
        }
        return values[index];
    }

    int index_of(K key) {
        int el = keys.get_elements();
        for (int i=0;i<el;i++) {
            if(keys[i] == key) {
                return i;
            }
        }
        return -1;
    }

    void set(K key, V value) {
        int i = index_of(key);
        if (i == -1) {
            append(key, value);
        } else {
            values.replace_idx(i, value);
        }
    }

	V operator[] (K key) {
        int el = keys.get_elements();
        if (!compare) {
            for (int i=0;i<el;i++) {
                if(keys[i] == key) {
                    return values[i];
                }
            }
        } else {
            for (int i=0;i<el;i++) {
                if(compare(keys[i], key) == 0) {
                    return values[i];
                }
            }
        }
        return defV;
	}

    void dump(void)
    {
        for(int i=0;i<keys.get_elements();i++) {
            printf(" '%s' => '%s'\n", keys[i], (const char *)values[i]);
        }
    }
};
#endif
