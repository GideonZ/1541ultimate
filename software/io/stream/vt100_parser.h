/*
 * vt100_parser.h
 *
 *  Created on: May 24, 2015
 *      Author: Gideon
 */

#ifndef IO_STREAM_VT100_PARSER_H_
#define IO_STREAM_VT100_PARSER_H_

// can be used / reused to collect color / reverse codes from VT100 stream (currently not used)
#include "screen.h"

class VT100_Parser
{
    // processing state
	char escape_cmd_data[16];
    bool escape;
    bool escape_cmd;
    int  escape_cmd_pos;

    Screen *owner; // it will call the functions to set color / mode on this object

    void  parseAttr();
    void  doAttr(int);
public:
    VT100_Parser(Screen *owner) : owner(owner) {
    	escape = false;
    	escape_cmd = false;
    	escape_cmd_pos = 0;
    }
    int processChar(char c); // returns 1 when char is printed, 0 otherwise.
};

#endif /* IO_STREAM_VT100_PARSER_H_ */
