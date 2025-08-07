#ifndef IEC_DRIVE_H
#define IEC_DRIVE_H

#include <stdint.h>
#include "menu.h"
#include "config.h"
#include "userinterface.h"
#include "subsys.h"
#include "fs_errors_flags.h"
#include "disk_image.h"
#include "iec_interface.h"

#define IEC_PARTITION_CONFIG "iec_partitions.ipr"

class IecChannel;
class IecCommandChannel;
class IecFileSystem;
class FileManager;
class IecDrive;

class IecDrive : public IecSlave, SubSystem, ObjectWithMenu, ConfigurableObject
{
    IecInterface *intf;
    int slot_id;

    int my_bus_id;
    bool enable;

    FileManager *fm;
    IecChannel *channels[16];
    int current_channel;

    int last_error_code;
    int last_error_track;
    int last_error_sector;
    uint8_t last_track;

    Path *cmd_path;
    IecFileSystem *vfs;
 
    static void set_iec_dir(IecSlave *obj, void *path);

    struct {
        Action *turn_on;
        Action *turn_off;
    	Action *reset;
        Action *set_dir;
        Action *save_part;
    } myActions;
public:
    IecDrive();
    virtual ~IecDrive();
    
    // From Subsystem
    SubsysResultCode_e executeCommand(SubsysCommand *cmd); // from SubSystem
    const char *identify(void) { return "IEC Drive"; }

    // From ObjectWithMenu
    void create_task_items();
    void update_task_items(bool writablePath, Path *path);

    // From ConfigurableObject
    void effectuate_settings(void); // from ConfigurableObject

    // From IecSlave
    bool is_enabled(void) { return enable; }
    uint8_t get_address(void) { return (uint8_t)my_bus_id; }
    uint8_t get_type(void) { return 0x0F; }
    const char *iec_identify(void) { return "IEC Drive"; }
    void info(JSON_Object *);
    void info(StreamTextLog&);

    void reset(void);
    void talk(void);

    t_channel_retval prefetch_data(uint8_t&);
    t_channel_retval prefetch_more(int, uint8_t*&, int &);
    t_channel_retval push_ctrl(uint16_t);
    t_channel_retval push_data(uint8_t);
    t_channel_retval pop_data(void);
    t_channel_retval pop_more(int);

    // Local Functions
    void set_error(int err, int track, int sector);
    void set_error_fres(FRESULT fres);

    int get_error_string(char *); // writes string into buffer
    IecCommandChannel *get_command_channel();
    IecCommandChannel *get_data_channel(int chan);
    const char *get_partition_dir(int p);
    void add_partition(int p, const char *path);

    friend class IecChannel;
    friend class IecCommandChannel;
};

#define ERR_ALL_OK						00
#define ERR_FILES_SCRATCHED				01
#define ERR_PARTITION_OK                02
#define ERR_READ_ERROR					20
#define ERR_WRITE_ERROR					25
#define ERR_WRITE_PROTECT_ON			26
#define ERR_DISK_ID_MISMATCH			29
#define ERR_SYNTAX_ERROR_GEN			30
#define ERR_SYNTAX_ERROR_CMD			31
#define ERR_SYNTAX_ERROR_CMDLENGTH		32
#define ERR_SYNTAX_ERROR_NAME			33
#define ERR_SYNTAX_ERROR_NONAME			34
#define ERR_SYNTAX_ERROR_CMD15			39
#define ERR_RECORD_NOT_PRESENT          50
#define ERR_OVERFLOW_IN_RECORD          51
#define ERR_WRITE_FILE_OPEN				60
#define ERR_FILE_NOT_OPEN				61
#define ERR_FILE_NOT_FOUND				62
#define ERR_FILE_EXISTS					63
#define ERR_FILE_TYPE_MISMATCH			64
#define ERR_FRESULT_CODE                69
#define ERR_NO_CHANNEL          		70
#define ERR_DIRECTORY_ERROR				71
#define ERR_DISK_FULL					72
#define ERR_DOS							73
#define ERR_DRIVE_NOT_READY				74
// custom
#define ERR_BAD_COMMAND					75
#define ERR_UNIMPLEMENTED				76
#define ERR_PARTITION_ERROR             77

typedef struct {
	uint8_t nr;
	char* msg;
	uint8_t len;
} IEC_ERROR_MSG;

extern IecDrive *iec_drive;

#endif
