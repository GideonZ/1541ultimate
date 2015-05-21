/*
 * factory_test.cc
 *
 *  Created on: May 21, 2015
 *      Author: Gideon
 */

#include "factory.h"
#include <string.h>
#include <stdio.h>

// static variable instantiation of factory pointer
template<class Ta, class Tb>
Factory<Ta, Tb>* Factory<Ta, Tb> :: factory;

int test_aap(const char *val) {
	if (strcmp(val, "aap") == 0)
		return 1;
	return 0;
}

int test_koe(const char *val) {
	if (strcmp(val, "koe") == 0)
		return 2;
	return 0;
}

// test static registration
FactoryRegistrator<const char *, int> register_koe(test_koe);

int main()
{
	// test dynamic registration
	Factory<const char *, int> *f = Factory<const char *, int> :: getFactory();
	f->register_type(test_aap);

	// test creation of objects.
	printf("%d\n", f->create("vogel"));
	printf("%d\n", f->create("aap"));
	printf("%d\n", f->create("koe"));

	return 0;
}
