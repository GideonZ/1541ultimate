#ifndef C1581_Channel_H_
#define C1581_Channel_H_

#include "c1581.h"
#include <stdint.h>


class C1581;
//class C1581_CommandChannel;

class C1581_Channel
{
	int write;
	int size;
	int last_command;

	t_channel_state state;
	mstring dirpattern;
	bool channelopen;
	uint8_t writetrack;
	uint8_t writesector;
	uint16_t writeblock;

	uint8_t localbuffer[65536];
	int blocknumber;
	uint8_t file_name[16];

public:
	uint8_t buffer[256];
	C1581_Channel();
	virtual ~C1581_Channel();
	virtual void init(C1581 *parent, int ch);
	virtual void reset_prefetch(void);
	virtual int prefetch_data(uint8_t& data);
	virtual int pop_data(void);
	int read_block(void);
	virtual int push_data(uint8_t b);
	virtual int push_command(uint8_t b);
	void parse_command(char *buffer, command_t *command);
	bool hasIllegalChars(const char *name);
	const char *GetExtension(char specifiedType, bool &explicitExt);
private:
	uint8_t open_file(void);
	uint8_t close_file(void);
	friend class C1581_CommandChannel;

protected:
	C1581* c1581;
	int channel;
	int pointer;
	int last_byte;
	int prefetch;
	int prefetch_max;
};

class C1581_CommandChannel : public C1581_Channel
{
    void mem_read(void);
    void mem_write(void);
    void renam(command_t& command);
    void copy(command_t& command);
    void format(command_t& command);
    void scratch(command_t& command);
    void u1(command_t& command);
    void u2(command_t& command);
    void mem_exec(command_t& command);
public:
    using C1581_Channel::operator=;
    C1581_CommandChannel();
    ~C1581_CommandChannel();
    void init(C1581 *parent, int ch);
    void get_last_error(int err, int track, int sector);
    int pop_data(void);
    int push_data(uint8_t b);
    void exec_command(command_t &command);
    int push_command(uint8_t b);
};

#endif
