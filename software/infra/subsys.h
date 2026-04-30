/*
 * subsys_command.h
 *
 *  Created on: Jun 18, 2015
 *      Author: Gideon
 */

#ifndef INFRA_SUBSYS_H_
#define INFRA_SUBSYS_H_

#include "mystring.h"
#include "action.h"
#include "managed_array.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdio.h>

#include "http_codes.h"

class SubsysCommand;
class UserInterface;

#define SUBSYSID_USER_STANDARD   0
#define SUBSYSID_C64             1
#define SUBSYSID_DRIVE_A         2
#define SUBSYSID_DRIVE_B         3
#define SUBSYSID_DRIVE_C         4
#define SUBSYSID_TAPE_PLAYER     5
#define SUBSYSID_TAPE_RECORDER   6
#define SUBSYSID_IEC             7
#define SUBSYSID_CMD_IF			 8
#define SUBSYSID_U64			 9
#define SUBSYSID_PRINTER         10
#define SUBSYSID_ULTICOPY        11

#define SORT_ORDER_ASSEMBLY 5
#define SORT_ORDER_CREATE  10
#define SORT_ORDER_C64     20
#define SORT_ORDER_DRIVES  30
#define SORT_ORDER_SOFTIEC 40
#define SORT_ORDER_TAPE    50
#define SORT_ORDER_PRINTER 60
#define SORT_ORDER_CONFIG  70
#define SORT_ORDER_DEVELOPER 999


class SubSystem  // implements function "executeCommand"
{
	int myID;
	SemaphoreHandle_t myMutex;
	friend class SubsysCommand;

	virtual SubsysResultCode_e executeCommand(SubsysCommand *cmd) { return SSRET_SUBSYS_NO_EXEC; }
public:
	SubSystem(int id) {
		myID = id;
		myMutex = xSemaphoreCreateMutex();
		SubSystem :: getSubSystems() -> set(myID, this);
	}
	virtual ~SubSystem() {
		SubSystem :: getSubSystems() -> unset(myID);
		vSemaphoreDelete(myMutex);
	}

	static ManagedArray<SubSystem *>* getSubSystems() {
		static ManagedArray<SubSystem *> subsystem_array(16, NULL);
		return &subsystem_array;
	}

	virtual const char *identify(void) { return "Unknown Subsystem"; }

	int  lock(const char *name) {
		int retval = xSemaphoreTake(myMutex, 5000);
		if (!retval) {
			printf("Lock: %s not available.\n", name);
		}
		return retval;
	}
	void unlock() {
		xSemaphoreGive(myMutex);
	}

	int getID() { return myID; }
};

// struct SubsysResult
// {
// 	int      resultCode;
// 	int		 resultDataSize;
// 	uint8_t *resultData;
// };

class SubsysCommand
{
public:
    // Only used in Stream Menu - an obsolete ASC-II menu that was once available on UART. It directly connects to an action
	SubsysCommand(UserInterface *ui, Action *act, const char *p = NULL, const char *fn = NULL) :
		user_interface(ui),
		subsysID(act->subsys),
		functionID(act->function),
		mode(act->mode),
		direct_call(act->func),
        direct_obj(act->getObject()),
		actionName(act->getName()),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	// The most common form of Subsys command; one that is called just about anywhere; with a reference to a path and file
	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, const char *p = NULL, const char *fn = NULL) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
        direct_obj(NULL),
		actionName(""),
		path(p), filename(fn) {
		buffer = NULL;
		bufferSize = 0;
	}

	// A variant of the one above, but then using a pointer to a buffer. This should be obsoleted.
	SubsysCommand(UserInterface *ui, int subID, int funcID, int mode, void *buffer, int bufferSize) :
		user_interface(ui),
		subsysID(subID),
		functionID(funcID),
		mode(mode),
		direct_call(0),
        direct_obj(NULL),
        actionName(""),
		path(""), filename(""),
		buffer(buffer),
		bufferSize(bufferSize) {
	}

    SubsysResultCode_t execute(void);
    
	void print(void) {
		printf("SubsysCommand for system %d:\n", subsysID);
		printf("  Function ID: %d\n", functionID);
		printf("  Mode: %d\n", mode);
		printf("  Path: %s\n", path.c_str());
		printf("  Filename: %s\n", filename.c_str());
		printf("  Buffer = %p (size: %d)\n", buffer, bufferSize);
	}

    static const char *error_string(SubsysResultCode_e resultCode)
    {
        static const char *error_strings[] = {
            NULL,        
            "Invalid Parameter",
            "Call requires user interaction, but no user interface object exists",
            "Generic Error",
            "Disk Error",
            "SubSystem does not exist", 
            "SubSystem does not implement command executer",
            "Could not obtain lock of subsystem",   
            "Disk has been modified, save first",    
            "Drive ROM not found",       
            "Drive ROM is invalid",
            "This hardware only supports 1541", 
            "Drive is in the wrong mode",
            "Cannot open file",
            "Seek operation on file failed",
            "Read operation on file failed",
            "Illegal mount mode / drive type",
            "Undefined subsystem command",
            "Operation aborted by user",
            "Save failed",
            "EEPROM data is too large",
            "EEPROM defined more than once",
            "ROM image is too large",
            "Error detected in file format",
            "This command is not supported on this architecture",
            "Out of memory",
            "MegaPatch3: Invalid image size",
            "MegaPatch3: DNP Image too large",
            "Internal Error",
            "SID File Memory Rollover",
            "Invalid Song Number Requested",
            "No Operational Network Interface",
            "Network Host Resolve Error",
        };
        return error_strings[(int)resultCode];
    }

    static int http_response_map(SubsysResultCode_e resultCode)
    {
        static const int codes[] = {
            HTTP_OK, // "All Okay",
            HTTP_BAD_REQUEST, // SSRET_INVALID_PARAMETER
            HTTP_SERVICE_UNAVAILABLE, // SSRET_NO_USER_INTERFACE
            HTTP_INTERNAL_SERVER_ERROR, // "Disk Error (Unspecified!)",    
            HTTP_INTERNAL_SERVER_ERROR, // "Generic Error (Unspecified!)",    
            HTTP_SERVICE_UNAVAILABLE, // "SubSystem does not exist", 
            HTTP_METHOD_NOT_ALLOWED, // "SubSystem does not implement command executer",
            HTTP_LOCKED, // "Could not obtain lock of subsystem",   
            HTTP_FORBIDDEN, // "Disk has been modified, save first",    
            HTTP_PRECONDITION_FAILED, // "Drive ROM not found",       
            HTTP_PRECONDITION_FAILED, // "Drive ROM is invalid",
            HTTP_METHOD_NOT_ALLOWED, // "This hardware only supports 1541", 
            HTTP_UNSUPPORTED_MEDIA_TYPE, // "Drive is in the wrong mode",
            HTTP_NOT_FOUND, // "Cannot open file",
            HTTP_INTERNAL_SERVER_ERROR, // "File seek failed"
            HTTP_INTERNAL_SERVER_ERROR, // "File read failed"
            HTTP_INTERNAL_SERVER_ERROR, // "Illegal mount mode / drive type",
            HTTP_INTERNAL_SERVER_ERROR, // "Undefined subsystem command",
            HTTP_INTERNAL_SERVER_ERROR, // "Aborted by user" <-- should not happen from HTTP
            HTTP_FAILED_DEPENDENCY, // "Save failed", not sure what went wrong, but the save was unsuccessful
            HTTP_PRECONDITION_FAILED, // EEPROM too large
            HTTP_PRECONDITION_FAILED, // EEPROM already defined
            HTTP_PRECONDITION_FAILED, // ROM Image is too large
            HTTP_UNSUPPORTED_MEDIA_TYPE, // Error detected in file format
            HTTP_NOT_IMPLEMENTED, // This command is not supported on this architecture.
            HTTP_INSUFFICIENT_STORAGE, // Internal out of memory error
            HTTP_BAD_REQUEST, // MP3 invalid size
            HTTP_BAD_REQUEST, // MP3 dnp too large
            HTTP_INTERNAL_SERVER_ERROR, //    SSRET_INTERNAL_ERROR,
            HTTP_UNSUPPORTED_MEDIA_TYPE, // SSRET_SID_ROLLOVER,
            HTTP_BAD_REQUEST, //  SSRET_INVALID_SONGNR,
            HTTP_INTERNAL_SERVER_ERROR, // NO NETWORK (should never happen)
            HTTP_NOT_FOUND, // NETWORK RESOLVE ERROR
        }; 
        return codes[(int)resultCode];
    }

    UserInterface *user_interface;
	int            subsysID;
	int			   functionID;
	int 		   mode;
	actionFunction_t direct_call;
    const void    *direct_obj;
	mstring        path;
	mstring		   filename;
	mstring        actionName;
	void 		  *buffer;
	int            bufferSize;
};

#endif /* INFRA_SUBSYS_H_ */
