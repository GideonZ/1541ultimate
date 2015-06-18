/*
 * subsys_command.h
 *
 *  Created on: Jun 18, 2015
 *      Author: Gideon
 */

#ifndef INFRA_SUBSYS_H_
#define INFRA_SUBSYS_H_

#include "globals.h"
#include "mystring.h"
#include "action.h"

class SubsysCommand;
class UserInterface;

#define SUBSYSID_USER_STANDARD   0
#define SUBSYSID_C64             1
#define SUBSYSID_DRIVE_A         2
#define SUBSYSID_DRIVE_B         3
#define SUBSYSID_TAPE_PLAYER     4
#define SUBSYSID_TAPE_RECORDER   5
#define SUBSYSID_IEC             6

class SubSystem  // implements function "executeCommand"
{
	int myID;
public:
	SubSystem(int id) {
		myID = id;
		Globals :: getSubSystems() -> set(myID, this);
	}
	virtual ~SubSystem() {
		Globals :: getSubSystems() -> unset(myID);
	}
	virtual const char *identify(void) { return "Unknown Subsystem"; }
	virtual int executeCommand(SubsysCommand *cmd) { return -2; }

	int getID() { return myID; }
};

struct SubsysResult
{
	int      resultCode;
	int		 resultDataSize;
	uint8_t *resultData;
};

class SubsysCommand
{
	SubSystem   *subsys;	// filled in by factory
public:
	SubsysCommand(UserInterface *ui, Action *act, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(act->subsys),
		functionID(act->function),
		mode(act->mode),
		direct_call(act->func),
		path(p), filename(fn) {
		subsys	       = 0;
	}

	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, const char *p, const char *fn) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
		path(p), filename(fn) {
		subsys	       = 0;
	}

	int execute(void) {
		if(direct_call) {
			return direct_call(this);
		}
		subsys = (*Globals :: getSubSystems())[subsysID];
		if (subsys) {
			return subsys->executeCommand(this);
		}
		return -1;
	}

	void print(void) {
		printf("SubsysCommand for system %d:\n", subsysID);
		printf("  Function ID: %d\n", functionID);
		printf("  Mode: %d\n", mode);
		printf("  Path: %s\n", path.c_str());
		printf("  Filename: %s\n", filename.c_str());
	}

	UserInterface *user_interface;
	int            subsysID;
	int			   functionID;
	int 		   mode;
	actionFunction_t direct_call;
	mstring        path;
	mstring		   filename;
};

#endif /* INFRA_SUBSYS_H_ */
