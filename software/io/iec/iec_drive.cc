#include "iec_drive.h"
#include "iec_channel.h"
#include "filemanager.h"
#include "userinterface.h"
#include "pattern.h"
#include "command_intf.h"
#include "init_function.h"
#include "json.h"

#ifndef FS_ROOT
#define FS_ROOT "/Usb0/"
#endif

#define MENU_IEC_ON          0xCA0E
#define MENU_IEC_OFF         0xCA0F
#define MENU_IEC_RESET       0xCA10
#define MENU_IEC_SET_DIR     0xCA17
   
#define CFG_IEC_ENABLE   0x51
#define CFG_IEC_BUS_ID   0x52
#define CFG_IEC_PATH     0x53

static struct t_cfg_definition iec_config[] = {
    { CFG_IEC_ENABLE,    CFG_TYPE_ENUM,   "IEC Drive",         "%s", en_dis, 0,  1, 0 },
    { CFG_IEC_BUS_ID,    CFG_TYPE_VALUE,  "Soft Drive Bus ID", "%d", NULL,   8, 30, 11 },
    { CFG_IEC_PATH,      CFG_TYPE_STRING, "Default Path",      "%s", NULL,   0, 30, (long int)FS_ROOT },
    { 0xFF, CFG_TYPE_END, "", "", NULL, 0, 0, 0 }
};

// this global will cause us to run!
IecDrive *iec_drive = NULL;
void init_iec_drive(void *_a, void *_b)
{
    iec_drive = new IecDrive();
}
InitFunction iec_drive_init("SoftIEC Drive", init_iec_drive, NULL, NULL, 11);

// Errors 

// Error,ErrorText(,Track,Sector\015) 
// The track and sector will be sent seperately

const char msg00[] = " OK";						//00
const char msg01[] = " FILES SCRATCHED";		//01	Track number shows how many files were removed
const char msg02[] = "PARTITION SELECTED";      //02
const char msg20[] = "READ ERROR"; 				//20 (Block Header Not Found)
//const char msg21[] = "READ ERROR"; 				//21 (No Sync Character)
//const char msg22[] = "READ ERROR"; 				//22 (Data Block not Present)
//const char msg23[] = "READ ERROR"; 				//23 (Checksum Error in Data Block)
//const char msg24[] = "READ ERROR"; 				//24 (Byte Decoding Error)
const char msg25[] = "WRITE ERROR";				//25 (Write/Verify Error)
const char msg26[] = "WRITE PROTECT ON";		//26
//const char msg27[] = "READ ERROR"; 				//27 (Checksum Error in Header)
//const char msg28[] = "WRITE ERROR"; 			//28 (Long Data Block)
const char msg29[] = "DISK ID MISMATCH";		//29
const char msg30[] = "SYNTAX ERROR";			//30 (General)
const char msg31[] = "SYNTAX ERROR";			//31 (Invalid Command)
const char msg32[] = "SYNTAX ERROR";			//32 (Command Line > 58 Characters)
const char msg33[] = "SYNTAX ERROR";			//33 (Invalid Filename)
const char msg34[] = "SYNTAX ERROR";			//34 (No File Given)
const char msg39[] = "SYNTAX ERROR";			//39 (Invalid Command)
const char msg50[] = "RECORD NOT PRESENT";		//50
const char msg51[] = "OVERFLOW IN RECORD";		//51
//const char msg52[] = "FILE TOO LARGE";			//52
const char msg60[] = "WRITE FILE OPEN";			//60
const char msg61[] = "FILE NOT OPEN";			//61
const char msg62[] = "FILE NOT FOUND";			//62
const char msg63[] = "FILE EXISTS";				//63
const char msg64[] = "FILE TYPE MISMATCH";		//64
//const char msg65[] = "NO BLOCK";				//65
//const char msg66[] = "ILLEGAL TRACK AND SECTOR";//66
//const char msg67[] = "ILLEGAL SYSTEM T OR S";	//67
const char msg69[] = "FILESYSTEM ERROR";        //69
const char msg70[] = "NO CHANNEL";	            //70
const char msg71[] = "DIRECTORY ERROR";			//71
const char msg72[] = "DISK FULL";				//72
const char msg73[] = "U64IEC ULTIMATE DOS V1.1";//73 DOS MISMATCH(Returns DOS Version)
const char msg74[] = "DRIVE NOT READY";			//74
const char msg77[] = "SELECTED PARTITION ILLEGAL"; //77
const char msg_c1[] = "BAD COMMAND";			//custom
const char msg_c2[] = "UNIMPLEMENTED";			//custom


const IEC_ERROR_MSG last_error_msgs[] = {
		{ 00,(char*)msg00,NR_OF_EL(msg00) - 1 },
		{ 01,(char*)msg01,NR_OF_EL(msg01) - 1 },
        { 02,(char*)msg02,NR_OF_EL(msg02) - 1 },
		{ 20,(char*)msg20,NR_OF_EL(msg20) - 1 },
//		{ 21,(char*)msg21,NR_OF_EL(msg21) - 1 },
//		{ 22,(char*)msg22,NR_OF_EL(msg22) - 1 },
//		{ 23,(char*)msg23,NR_OF_EL(msg23) - 1 },
//		{ 24,(char*)msg24,NR_OF_EL(msg24) - 1 },
		{ 25,(char*)msg25,NR_OF_EL(msg25) - 1 },
		{ 26,(char*)msg26,NR_OF_EL(msg26) - 1 },
//		{ 27,(char*)msg27,NR_OF_EL(msg27) - 1 },
//		{ 28,(char*)msg28,NR_OF_EL(msg28) - 1 },
		{ 29,(char*)msg29,NR_OF_EL(msg29) - 1 },
		{ 30,(char*)msg30,NR_OF_EL(msg30) - 1 },
		{ 31,(char*)msg31,NR_OF_EL(msg31) - 1 },
		{ 32,(char*)msg32,NR_OF_EL(msg32) - 1 },
		{ 33,(char*)msg33,NR_OF_EL(msg33) - 1 },
		{ 34,(char*)msg34,NR_OF_EL(msg34) - 1 },
		{ 39,(char*)msg39,NR_OF_EL(msg39) - 1 },
		{ 50,(char*)msg50,NR_OF_EL(msg50) - 1 },
		{ 51,(char*)msg51,NR_OF_EL(msg51) - 1 },
//		{ 52,(char*)msg52,NR_OF_EL(msg52) - 1 },
		{ 60,(char*)msg60,NR_OF_EL(msg60) - 1 },
		{ 61,(char*)msg61,NR_OF_EL(msg61) - 1 },
		{ 62,(char*)msg62,NR_OF_EL(msg62) - 1 },
		{ 63,(char*)msg63,NR_OF_EL(msg63) - 1 },
		{ 64,(char*)msg64,NR_OF_EL(msg64) - 1 },
//		{ 65,(char*)msg65,NR_OF_EL(msg65) - 1 },
//		{ 66,(char*)msg66,NR_OF_EL(msg66) - 1 },
//		{ 67,(char*)msg67,NR_OF_EL(msg67) - 1 },
        { 69,(char*)msg69,NR_OF_EL(msg69) - 1 },
		{ 70,(char*)msg70,NR_OF_EL(msg70) - 1 },
		{ 71,(char*)msg71,NR_OF_EL(msg71) - 1 },
		{ 72,(char*)msg72,NR_OF_EL(msg72) - 1 },
		{ 73,(char*)msg73,NR_OF_EL(msg73) - 1 },
		{ 74,(char*)msg74,NR_OF_EL(msg74) - 1 },
        { 77,(char*)msg77,NR_OF_EL(msg77) - 1 },

		{ 75,(char*)msg_c1,NR_OF_EL(msg_c1) - 1 },
		{ 76,(char*)msg_c2,NR_OF_EL(msg_c2) - 1 }
};

IecDrive :: IecDrive() : SubSystem(SUBSYSID_IEC)
{
    intf = IecInterface :: get_iec_interface();
	fm = FileManager :: getFileManager();
    my_bus_id = 0;

    register_store(0x49454300, "SoftIEC Drive Settings", iec_config);

    enable = false;
    cmd_path = fm->get_new_path("IEC Gui Path");

    last_error_code = ERR_DOS;
    last_error_track = 0;
    last_error_sector = 0;
    current_channel = 0;
    my_bus_id = 10;

    effectuate_settings();

    vfs = new IecFileSystem(this);

    for(int i=0;i<15;i++) {
        channels[i] = new IecChannel(this, i);
    }
    channels[15] = new IecCommandChannel(this, 15);

    // Register and configure the processor
    slot_id = intf->register_slave(this);
    intf->configure();
}

IecDrive :: ~IecDrive()
{
    for(int i=0;i<16;i++)
        delete channels[i];

    fm->release_path(cmd_path);
    delete vfs;
    intf->unregister_slave(slot_id);
}

IecCommandChannel *IecDrive :: get_command_channel(void)
{
    return (IecCommandChannel *)channels[15];
}

IecCommandChannel *IecDrive :: get_data_channel(int chan)
{
    return (IecCommandChannel *)channels[chan & 15];
}

void IecDrive :: effectuate_settings(void)
{
    my_bus_id = cfg->get_value(CFG_IEC_BUS_ID);
    cmd_if.set_kernal_device_id(my_bus_id);
    
    rootPath = cfg->get_string(CFG_IEC_PATH);
    enable = uint8_t(cfg->get_value(CFG_IEC_ENABLE));

    intf->configure();
}

const char *IecDrive :: get_root_path(void)
{
    return rootPath;
}

void IecDrive :: create_task_items(void)
{
    TaskCategory *iec = TasksCollection :: getCategory("Software IEC", SORT_ORDER_SOFTIEC);
    myActions.turn_on	 = new Action("Turn On",        SUBSYSID_IEC, MENU_IEC_ON);
    myActions.reset      = new Action("Reset",          SUBSYSID_IEC, MENU_IEC_RESET);
    myActions.set_dir    = new Action("Set dir. here",  SUBSYSID_IEC, MENU_IEC_SET_DIR);
    myActions.turn_off	 = new Action("Turn Off",       SUBSYSID_IEC, MENU_IEC_OFF);

    iec->append(myActions.turn_on);
    iec->append(myActions.turn_off);
    iec->append(myActions.reset);
    iec->append(myActions.set_dir);
}

// called from GUI task
void IecDrive :: update_task_items(bool writablePath, Path *path)
{
	if (enable) {
		myActions.turn_off->show();
		myActions.turn_on->hide();
	} else {
		myActions.turn_on->show();
		myActions.turn_off->hide();
	}
}

// called from GUI task
SubsysResultCode_e IecDrive :: executeCommand(SubsysCommand *cmd)
{
    const char *path;
    char *pathcopy;
    iec_closure_t cb;

    cmd_path->cd(cmd->path.c_str());

	switch(cmd->functionID) {
		case MENU_IEC_ON:
			enable = 1;
			cfg->set_value(CFG_IEC_ENABLE, enable);
            intf->configure();
			break;
		case MENU_IEC_OFF:
			enable = 0;
			cfg->set_value(CFG_IEC_ENABLE, enable);
            intf->configure();
			break;
		case MENU_IEC_RESET:
            reset();
			break;
		case MENU_IEC_SET_DIR:
            path = cmd->path.c_str();
            pathcopy = new char[strlen(path) + 1];
            strcpy(pathcopy, path);
            cb = { this, set_iec_dir, pathcopy };
            intf->run_from_iec(&cb);
    	    break;
		default:
			break;
    }
    return SSRET_OK;
}

void IecDrive :: reset(void)
{
    effectuate_settings();
    IecPartition *p = vfs->GetPartition(0);
    p->cd(rootPath);
    for(int i=0; i < 16; i++) {
        channels[i]->reset();
    }
    vfs->SetCurrentPartition(0);
    last_error_code = ERR_DOS;
    last_error_track = 0;
    last_error_sector = 0;
    current_channel = 0;
}

t_channel_retval IecDrive :: prefetch_data(uint8_t& data)
{
    return channels[current_channel]->prefetch_data(data);
}

t_channel_retval IecDrive :: prefetch_more(int bufsize, uint8_t*&pointer, int &available)
{
    return channels[current_channel]->prefetch_more(bufsize, pointer, available);
}

t_channel_retval IecDrive :: push_ctrl(uint16_t ctrl)
{
    switch(ctrl) {
        case SLAVE_CMD_ATN:
            current_channel = 0;
            // reset_prefetch();
            break;
        case SLAVE_CMD_EOI:
            return channels[current_channel]->push_command(0x00);
            break;
        default:
            current_channel = (ctrl & 15);
            uint8_t cmd = (uint8_t)(ctrl & 0xF0);
            return channels[current_channel]->push_command(cmd);
    }
    return IEC_OK;
}

t_channel_retval IecDrive :: push_data(uint8_t data)
{
    return channels[current_channel]->push_data(data);
}

t_channel_retval IecDrive :: pop_data(void)
{
    return channels[current_channel]->pop_data();
}

t_channel_retval IecDrive :: pop_more(int byte_count)
{
    return channels[current_channel]->pop_more(byte_count);
}

void IecDrive :: talk(void)
{
    channels[current_channel]->talk();
}

// called from IEC task, statically
void IecDrive :: set_iec_dir(IecSlave *sl, void *data)
{
    IecDrive *drive = (IecDrive *)sl;
    const char *pathstring = (const char *)data;
    IecPartition *p = drive->vfs->GetPartition(0);
    p->cd(pathstring);
    delete[] pathstring;
}

// called from critical section
const char *IecDrive :: get_partition_dir(int p)
{
    return vfs->GetPartitionPath(p, false);
}
                
void IecDrive :: set_error(int code, int track, int sector)
{
    last_error_code = code;
    last_error_track = track;
    last_error_sector = sector;
}

void IecDrive :: set_error_fres(FRESULT fres)
{
    int tr=0, sec=0, err = 0;

    switch (fres) {
    case FR_OK:                  err = ERR_ALL_OK;            /* (0) Succeeded */
        break;
    case FR_DISK_ERR:            err = ERR_DRIVE_NOT_READY;   /* (1) A hard error occurred in the low level disk I/O layer */
        break;
    case FR_INT_ERR:             err = ERR_FRESULT_CODE;      /* (2) Assertion failed */
        break;
    case FR_NOT_READY:           err = ERR_DRIVE_NOT_READY;   /* (3) The physical drive cannot work */
        break;
    case FR_NO_FILE:             err = ERR_FILE_NOT_FOUND;    /* (4) Could not find the file */
        break;
    case FR_NO_PATH:             err = ERR_FILE_NOT_FOUND;    /* (5) Could not find the path */
        break;
    case FR_INVALID_NAME:        err = ERR_SYNTAX_ERROR_NAME; /* (6) The path name format is invalid */
        break;
    case FR_DENIED:              err = ERR_DIRECTORY_ERROR;   /* (7) Access denied due to prohibited access or directory full */
        break;
    case FR_EXIST:               err = ERR_FILE_EXISTS;       /* (8) Access denied due to prohibited access */
        break;
    case FR_INVALID_OBJECT:      err = ERR_FRESULT_CODE;      /* (9) The file/directory object is invalid */
        break;
    case FR_WRITE_PROTECTED:     err = ERR_WRITE_PROTECT_ON;  /* (10) The physical drive is write protected */
        break;
    case FR_INVALID_DRIVE:       err = ERR_DRIVE_NOT_READY;   /* (11) The logical drive number is invalid */
        break;
    case FR_NOT_ENABLED:         err = ERR_DRIVE_NOT_READY;   /* (12) The volume has no work area */
        break;
    case FR_NO_FILESYSTEM:       err = ERR_DRIVE_NOT_READY;   /* (13) There is no valid FAT volume */
        break;
    case FR_MKFS_ABORTED:        err = ERR_FRESULT_CODE;      /* (14) The f_mkfs() aborted due to any parameter error */
        break;
    case FR_TIMEOUT:             err = ERR_DRIVE_NOT_READY;   /* (15) Could not get a grant to access the volume within defined period */
        break;
    case FR_LOCKED:              err = ERR_WRITE_FILE_OPEN;   /* (16) The operation is rejected according to the file sharing policy */
        break;
    case FR_NO_MEMORY:           err = ERR_FRESULT_CODE;      /* (17) LFN working buffer could not be allocated */
        break;
    case FR_TOO_MANY_OPEN_FILES: err = ERR_FRESULT_CODE;      /* (18) Number of open files > _FS_SHARE */
        break;
    case FR_INVALID_PARAMETER:   err = ERR_FRESULT_CODE;      /* (19) Given parameter is invalid */
        break;
    case FR_DISK_FULL:           err = ERR_DISK_FULL;         /* (20) OLD FATFS: no more free clusters */
        break;
    case FR_DIR_NOT_EMPTY:       err = ERR_FILE_EXISTS;       /* (21) Directory not empty */
        break;
    default:                     err = ERR_FRESULT_CODE;
    }
    if (err == ERR_FRESULT_CODE) {
        tr = fres;
    }
    set_error(err, tr, sec);
}

int IecDrive ::get_error_string(char *buffer)
{
    int len;
    for (int i = 0; i < NR_OF_EL(last_error_msgs); i++) {
        if (last_error_code == last_error_msgs[i].nr) {
            return sprintf(buffer, "%02d,%s,%02d,%02d\015", last_error_code, last_error_msgs[i].msg,
                            last_error_track, last_error_sector);
        }
    }
    return sprintf(buffer, "99,UNKNOWN,00,00\015");
}
    
void IecDrive :: info(StreamTextLog &b)
{
    char buffer[64];
    if(enable) {
        iec_drive->get_error_string(buffer);
        b.format("    Error string:\n    %s\n", buffer);
        for (int i=0; i < MAX_PARTITIONS; i++) {
            const char *p = iec_drive->get_partition_dir(i);
            if (p) {
                b.format("    Partition%3d: %s\n", i, p);
            }
        }
    }
    b.format("\n");
}

void IecDrive :: info(JSON_Object *obj)
{
    char buffer[64];
    int len = iec_drive->get_error_string(buffer);
    if (len) {
        buffer[len-1] = 0; // Strip CR
    }
    
    JSON_List *partitions = JSON::List();
    for (int i=0; i < MAX_PARTITIONS; i++) {
        const char *p = iec_drive->get_partition_dir(i);
        if (p) {
            partitions->add(JSON::Obj()->add("id", i)->add("path", p));
        }
    }

    obj ->add("type", "DOS emulation")
        ->add("last_error", buffer)
        ->add("partitions", partitions);
}
