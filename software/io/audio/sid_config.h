/*
 * sid_config.h
 *
 *  Created on: Aug 30, 2018
 *      Author: gideon
 */

#ifndef SID_CONFIG_H_
#define SID_CONFIG_H_

typedef struct _t_sid_definition
{
    uint8_t baseAddress;
    uint8_t sidType; // 0 = any, 1 = 6581, 2 = 8580, 3 = both 6581 and 8580
} t_sid_definition;

bool SidAutoConfig(int count, t_sid_definition *requested);

#endif /* SID_CONFIG_H_ */
