#ifndef RETURN_CODES_H
#define RETURN_CODES_H

typedef enum {
	SSRET_OK = 0,
    SSRET_INVALID_PARAMETER,
    SSRET_NO_USER_INTERFACE,
	SSRET_GENERIC_ERROR,
    SSRET_DISK_ERROR,
	SSRET_SUBSYS_NOT_PRESENT,
	SSRET_SUBSYS_NO_EXEC,
	SSRET_NO_LOCK,
	SSRET_DISK_MODIFIED,
	SSRET_NO_DRIVE_ROM,
	SSRET_INVALID_DRIVE_ROM,
	SSRET_ONLY_1541,
	SSRET_WRONG_DRIVE_TYPE,
	SSRET_CANNOT_OPEN_FILE,
	SSRET_FILE_SEEK_FAILED,
	SSRET_FILE_READ_FAILED,
	SSRET_WRONG_MOUNT_MODE,
	SSRET_UNDEFINED_COMMAND,
	SSRET_ABORTED_BY_USER,
	SSRET_SAVE_FAILED,
	SSRET_EEPROM_TOO_LARGE,
	SSRET_EEPROM_ALREADY_DEFINED,
	SSRET_ROM_IMAGE_TOO_LARGE,
	SSRET_ERROR_IN_FILE_FORMAT,
	SSRET_NOT_IMPLEMENTED,
    SSRET_OUT_OF_MEMORY,
    SSRET_MP3_INVALID_SIZE,
    SSRET_MP3_DNP_TOO_LARGE,
    SSRET_INTERNAL_ERROR,
    SSRET_SID_ROLLOVER,
    SSRET_INVALID_SONGNR,
    SSRET_NO_NETWORK,
    SSRET_NETWORK_RESOLVE_ERROR,
} SubsysResultCode_e;

/*
typedef enum {
	MENU_NOP = 0,
	MENU_ENTER = 1,
	MENU_BACK = 2,
	MENU_EXIT = 3,
} SubsysMenuAction_e;
*/

typedef struct {
	SubsysResultCode_e status;
//	SubsysMenuAction_e menu;
} SubsysResultCode_t;

#endif // RETURN_CODES_H