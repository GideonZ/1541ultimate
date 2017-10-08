/*
 * u64_config.h
 *
 *  Created on: Oct 6, 2017
 *      Author: Gideon
 */

#ifndef U64_CONFIG_H_
#define U64_CONFIG_H_

#include "config.h"

class U64Config : public ConfigurableObject
{
public:
    U64Config();
    ~U64Config() {}

    void effectuate_settings();
};

extern U64Config u64_configurator;

#endif /* U64_CONFIG_H_ */
