/*
 * system_info.h
 *
 *  Created on: Jul 17, 2021
 *      Author: gideon
 */

#ifndef SOFTWARE_USERINTERFACE_SYSTEM_INFO_H_
#define SOFTWARE_USERINTERFACE_SYSTEM_INFO_H_

#include "userinterface.h"
#include "stream_textlog.h"


class C1541;
class SystemInfo {
    static void drive_info(StreamTextLog &b, C1541 *drive, char letter);
    static void cart_info(StreamTextLog &b);
    static void storage_info(StreamTextLog &b);
    static char*size_expression(uint64_t a, uint64_t b, char *buf);
    static void hw_modules(StreamTextLog &b, uint16_t bits, const char *msg);

public:
    static void generate(UserInterface *ui);
};


#endif /* SOFTWARE_USERINTERFACE_SYSTEM_INFO_H_ */
