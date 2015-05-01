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
public:
    Factory() : types(4, 0) { }
    ~Factory() { }

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
	FactoryRegistrator(Factory<Tobj, Treturn> &factory, function_t func) {
		factory.register_type(func);
	}
};

#endif /* COMPONENTS_FACTORY_H_ */
