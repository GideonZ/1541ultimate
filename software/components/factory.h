/*
 * factory.h
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#ifndef COMPONENTS_FACTORY_H_
#define COMPONENTS_FACTORY_H_

#include "indexed_list.h"

template <class Tobj, class Treturn>
class Factory
{
	typedef Treturn (*function_t)(Tobj p);

	IndexedList<function_t> types;
    Factory() : types(4, 0) { }
    ~Factory() { }

    static Factory <Tobj, Treturn>* factory;
public:
    static Factory <Tobj, Treturn>* getFactory() {
    	if (!factory) {
    		factory = new Factory <Tobj, Treturn>();
    	}
    	return factory;
    }

    void register_type(function_t tester) {
    	types.append(tester);
    }

    Treturn create(Tobj obj) {
        for(int i=0;i<types.get_elements();i++) {
        	function_t tester = types[i];
            Treturn matched = tester(obj);
            if(matched)
                return matched;
        }
        return 0;
    }
};

template <class Tobj, class Treturn>
class FactoryRegistrator
{
	typedef Treturn (*function_t)(Tobj p);
public:
	FactoryRegistrator(function_t func) {
		Factory<Tobj, Treturn> *f = Factory<Tobj, Treturn> :: getFactory();
		f->register_type(func);
	}
};

#endif /* COMPONENTS_FACTORY_H_ */
