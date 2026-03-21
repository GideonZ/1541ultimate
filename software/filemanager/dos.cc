#include "dos.h"
#include "userinterface.h"
#include "home_directory.h"
#include "endianness.h"
#include "rtc.h"
#include <string.h>
#include "c64.h"

static const char* wdnames[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// crate and register ourselves!
Dos dos1(1);
Dos dos2(2);

static Message c_message_identification_dos = { 20, true, (uint8_t *)"ULTIMATE-II DOS V1.2" };
static Message c_status_directory_empty     = { 18, true, (uint8_t *)"01,DIRECTORY EMPTY" };
static Message c_status_truncated           = { 20, true, (uint8_t *)"02,REQUEST TRUNCATED" };
static Message c_status_not_implemented     = { 27, true, (uint8_t *)"99,FUNCTION NOT IMPLEMENTED" };
static Message c_status_no_data             = { 19, true, (uint8_t *)"81,NOT IN DATA MODE" };
static Message c_status_file_not_found      = { 17, true, (uint8_t *)"82,FILE NOT FOUND" };
static Message c_status_no_such_dir         = { 20, true, (uint8_t *)"83,NO SUCH DIRECTORY" };
static Message c_status_no_file_to_close    = { 19, true, (uint8_t *)"84,NO FILE TO CLOSE" };
static Message c_status_file_not_open       = { 15, true, (uint8_t *)"85,NO FILE OPEN" };
static Message c_status_cannot_read_dir     = { 23, true, (uint8_t *)"86,CAN'T READ DIRECTORY" };
static Message c_status_internal_error      = { 17, true, (uint8_t *)"87,INTERNAL ERROR" };
static Message c_status_no_information      = { 27, true, (uint8_t *)"88,NO INFORMATION AVAILABLE" };
static Message c_status_not_a_disk_image    = { 19, true, (uint8_t *)"89,NOT A DISK IMAGE" };
static Message c_status_drive_not_present   = { 20, true, (uint8_t *)"90,DRIVE NOT PRESENT" };
static Message c_status_incompatible_image  = { 21, true, (uint8_t *)"91,INCOMPATIBLE IMAGE" };
static Message c_status_prohibited          = { 22, true, (uint8_t *)"98,FUNCTION PROHIBITED" };

Dos::Dos(int id) :
        directoryList(16, NULL) {
    command_targets[id] = this;
    data_message.message = new uint8_t[512];
    status_message.message = new uint8_t[80];
    fm = FileManager::getFileManager();
    path = fm->get_new_path("Dos");
    file = 0;
    dir_entries = remaining = current_index = 0;
    dos_state = e_dos_idle;
}

Dos::~Dos() {
    fm->release_path(path);
    delete[] data_message.message;
    delete[] status_message.message;
    // officially we should deregister ourselves, but as this will only occur at application exit, there is no need
}

void Dos::cd(Message *command, Message **reply, Message **status) {
    mstring old_path(path->get_path());

    *reply = &c_message_empty;
    command->message[command->length] = 0;
    path->cd((char *) &command->message[2]);

    cleanupDirectory();
    FRESULT fres = fm->get_directory(path, directoryList, NULL);
    if (fres == FR_OK) {
        *status = &c_status_ok;
    } else {
        *status = &c_status_no_such_dir;
        path->cd(old_path.c_str());
    }
}

C1541* Dos::getDriveByID(uint8_t id) {
    C1541* drive = NULL;

    if (id) {
        if (c1541_A != NULL && c1541_A->get_drive_power() && c1541_A->get_effective_iec_address() == id) {
            drive = c1541_A;
        } else if (c1541_B != NULL && c1541_B->get_drive_power() 
                && c1541_B->get_effective_iec_address() == id) {
            drive = c1541_B;
        }
    } else {
        drive = C1541::get_last_mounted_drive();
        drive = drive != NULL ? drive : c1541_A;
    }
    return drive;
}

void Dos::parse_command(Message *command, Message **reply, Message **status) {
    // the first byte of the command lead us here to this function.
    // the second byte is the command byte for the DOS; the third and forth byte are the length
    // data follows after the 4th byte and is thus aligned for copy.    
    uint32_t pos, addr, len;
    uint32_t transferred = 0;
    FRESULT res = FR_OK;
    FileInfo *ffi;
    mstring str;
    char *oldname;
    char *newname;
    char *filename;
    char *destination;

    uint8_t drive_id;
    C1541 *drive;
    int mount_type, image_type;
    Action* mount_action;
    SubsysCommand* mount_command;
    SubsysCommand* swap_command;

    switch (command->message[1]) {
    case DOS_CMD_IDENTIFY:
        *reply = &c_message_identification_dos;
        *status = &c_status_ok;
        break;
    case DOS_CMD_OPEN_FILE:
        command->message[command->length] = 0;
        res = fm->fopen(path, (char *) &command->message[3],
                command->message[2], &file);
        *reply = &c_message_empty;
        if (!file) {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        } else {
            *status = &c_status_ok;
        }
        break;
    case DOS_CMD_CLOSE_FILE:
        *reply = &c_message_empty;
        if (file) {
            fm->fclose(file);
            file = NULL;
            *status = &c_status_ok;
        } else {
            *status = &c_status_no_file_to_close;
        }
        break;
    case DOS_CMD_FILE_SEEK:
        *reply = &c_message_empty;
        *status = &c_status_ok;
        if (!file) {
            *status = &c_status_file_not_open;
        } else {
            pos = (((uint32_t) command->message[5]) << 24)
                    | (((uint32_t) command->message[4]) << 16)
                    | (((uint32_t) command->message[3]) << 8)
                    | command->message[2];
            res = file->seek(pos);
            if (res != FR_OK) {
                strcpy((char *) status_message.message,
                        FileSystem::get_error_string(res));
                status_message.length = strlen((char *) status_message.message);
                *status = &status_message;
            }
        }
        break;
    case DOS_CMD_FILE_INFO:
        *reply = &c_message_empty;
        if (!file) {
            *status = &c_status_file_not_open;
            break;
        }
        *status = &c_status_ok;
        ffi = new FileInfo(INFO_SIZE);
        res = fm->fstat(file->get_path(), *ffi);
        if (res != FR_OK) {
            *status = &c_status_file_not_found;
            delete ffi;
            break;
        }
        dos_info.size = cpu_to_32le(ffi->size);
        dos_info.date = cpu_to_16le(ffi->date);
        dos_info.time = cpu_to_16le(ffi->time);
        dos_info.extension[0] = ' ';
        dos_info.extension[1] = ' ';
        dos_info.extension[2] = ' ';
        strncpy(dos_info.extension, ffi->extension, 3);
        dos_info.attrib = ffi->attrib;
        strncpy(dos_info.filename, ffi->lfname, 63);
        data_message.length = strlen(dos_info.filename) + 4 + 2 + 2 + 4;
        data_message.last_part = true;
        memcpy(data_message.message, &dos_info, data_message.length);
        *reply = &data_message;
        delete ffi;
        break;
    case DOS_CMD_FILE_STAT:
        *reply = &c_message_empty;
        *status = &c_status_ok;
        ffi = new FileInfo(INFO_SIZE);
        res = fm->fstat(path, (char *) &command->message[2], *ffi);
        if (res != FR_OK) {
            *status = &c_status_file_not_found;
            delete ffi;
            break;
        }
        dos_info.size = cpu_to_32le(ffi->size);
        dos_info.date = cpu_to_16le(ffi->date);
        dos_info.time = cpu_to_16le(ffi->time);
        dos_info.extension[0] = ' ';
        dos_info.extension[1] = ' ';
        dos_info.extension[2] = ' ';
        strncpy(dos_info.extension, ffi->extension, 3);
        dos_info.attrib = ffi->attrib;
        strncpy(dos_info.filename, ffi->lfname, 63);
        data_message.length = strlen(dos_info.filename) + 4 + 2 + 2 + 4;
        data_message.last_part = true;
        memcpy(data_message.message, &dos_info, data_message.length);
        *reply = &data_message;
        delete ffi;
        break;
    case DOS_CMD_DELETE_FILE:
        *reply = &c_message_empty;
        command->message[command->length] = 0;
        res = fm->delete_file(path, (char *) &command->message[2]);

        if (res == FR_OK) {
            *status = &c_status_ok;
        } else {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_RENAME_FILE:
        *reply = &c_message_empty;
        command->message[command->length] = 0;
        oldname = (char *) &command->message[2];
        newname = (char *) &command->message[2 + strlen(oldname) + 1];
        res = fm->rename(path, oldname, newname);

        if (res == FR_OK) {
            *status = &c_status_ok;
        } else {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_COPY_FILE:
        *reply = &c_message_empty;
        command->message[command->length] = 0;
        filename = (char *) &command->message[2];
        destination = (char *) &command->message[2 + strlen(filename) + 1];
        res = fm->fcopy(path->get_path(), filename, destination, filename, false);

        if (res == FR_OK) {
            *status = &c_status_ok;
        } else {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_LOAD_REU:
        if (!file) {
            *status = &c_status_file_not_open;
            *reply = &c_message_empty;
            break;
        }
        *status = &c_status_ok;
        addr = (((uint32_t) command->message[5]) << 24)
                | (((uint32_t) command->message[4]) << 16)
                | (((uint32_t) command->message[3]) << 8) | command->message[2];
        len = (((uint32_t) command->message[9]) << 24)
                | (((uint32_t) command->message[8]) << 16)
                | (((uint32_t) command->message[7]) << 8) | command->message[6];
        addr &= 0x00FFFFFF;
        // For "len", we need one bit more in order to save the whole 16 MB REU
        len &= 0x01FFFFFF;
        if ((addr + len) > 0x01000000) {
            len = 0x01000000 - addr;
            *status = &c_status_truncated;
        }
        res = file->read((uint8_t *) (addr | 0x01000000), len, &transferred);
        *reply = &data_message;
        sprintf((char *) data_message.message, "$%6x BYTES LOADED TO REU $%6x",
                transferred, addr);
        data_message.length = 35;
        data_message.last_part = true;
        if (res != FR_OK) {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_SAVE_REU:
        if (!file) {
            *status = &c_status_file_not_open;
            *reply = &c_message_empty;
            break;
        }
        *status = &c_status_ok;
        addr = (((uint32_t) command->message[5]) << 24)
                | (((uint32_t) command->message[4]) << 16)
                | (((uint32_t) command->message[3]) << 8) | command->message[2];
        len = (((uint32_t) command->message[9]) << 24)
                | (((uint32_t) command->message[8]) << 16)
                | (((uint32_t) command->message[7]) << 8) | command->message[6];
        addr &= 0x00FFFFFF;
        // For "len", we need one bit more in order to save the whole 16 MB REU
        len &= 0x01FFFFFF;
        if ((addr + len) > 0x01000000) {
            len = 0x01000000 - addr;
            *status = &c_status_truncated;
        }
        res = file->write((uint8_t *) (addr | 0x01000000), len, &transferred);
        *reply = &data_message;
        sprintf((char *) data_message.message, "$%6x BYTES SAVED FROM REU $%6x",
                transferred, addr);
        data_message.length = 36;
        data_message.last_part = true;
        if (res != FR_OK) {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_MOUNT_DISK:
        *reply = &c_message_empty;
        *status = &c_status_ok;
        command->message[command->length] = 0;

        drive_id = command->message[2];
        filename = (char *) &command->message[3];

        if ((drive = getDriveByID(drive_id)) == NULL) {
            *status = &c_status_drive_not_present;
            break;
        }

        ffi = new FileInfo(INFO_SIZE);
        res = fm->fstat(path, filename, *ffi);

        if (res != FR_OK) {
            *status = &c_status_file_not_found;
            delete ffi;
            break;
        }

        if (strncasecmp(ffi->extension, "d64", 3) == 0) {
            mount_type = MENU_1541_MOUNT_D64;
            image_type = 1541;
        } else if (strncasecmp(ffi->extension, "d71", 3) == 0) {
            mount_type = MENU_1541_MOUNT_D64;
            image_type = 1571;
        } else if (strncasecmp(ffi->extension, "d81", 3) == 0) {
            mount_type = MENU_1541_MOUNT_D64;
            image_type = 1581;
        } else if (strncasecmp(ffi->extension, "g64", 3) == 0) {
            mount_type = MENU_1541_MOUNT_G64;
            image_type = 1541;
        } else if (strncasecmp(ffi->extension, "g71", 3) == 0) {
            mount_type = MENU_1541_MOUNT_G64;
            image_type = 1571;
        } else {
            *status = &c_status_not_a_disk_image;
            delete ffi;
            break;
        }
        if (!(getFpgaCapabilities() & CAPAB_MM_DRIVE) && (image_type != 1541)) {
            *status = &c_status_incompatible_image;
            delete ffi;
            break;
        }

        mount_action = new Action("Mount Disk", drive->getID(), mount_type, image_type);
        mount_command = new SubsysCommand((UserInterface*) NULL, mount_action, path->get_path(), filename);
        mount_command->execute();

        delete ffi;
        delete mount_action;
        break;
    case DOS_CMD_UMOUNT_DISK:
        *reply = &c_message_empty;
        *status = &c_status_ok;

        drive_id = command->message[2];

        if ((drive = getDriveByID(drive_id)) == NULL) {
            *status = &c_status_drive_not_present;
            break;
        }

        mount_action = new Action("Umount Disk", drive->getID(),
                MENU_1541_REMOVE, 0);
        mount_command = new SubsysCommand((UserInterface*) NULL, mount_action,
                (char*) NULL, (char*) NULL);
        mount_command->execute();

        delete mount_action;
        break;
    case DOS_CMD_SWAP_DISK:
        *reply = &c_message_empty;
        *status = &c_status_ok;

        drive_id = command->message[2];

        if ((drive = getDriveByID(drive_id)) == NULL) {
            *status = &c_status_drive_not_present;
            break;
        }

        swap_command = new SubsysCommand((UserInterface*) NULL, drive->getID(),
                MENU_1541_SWAP, 0, (const char*) NULL, (const char*) NULL);

        swap_command->execute();
        break;
    case DOS_CMD_CHANGE_DIR:
        cd(command, reply, status);
        break;
    case DOS_CMD_COPY_UI_PATH:
        *reply = &c_message_empty;
        *status = &c_status_not_implemented;
        break;
    case DOS_CMD_COPY_HOME_PATH:
        destination = (char*) HomeDirectory::getHomeDirectory();
        strncpy((char*) command->message + 2, destination,
                strlen(destination) + 1);
        command->length = 2 + strlen(destination) + 1;
        cd(command, reply, status);
        // fallthrough

    case DOS_CMD_GET_PATH:
        strcpy((char *) data_message.message, path->get_path());
        data_message.length = strlen((char *) data_message.message);
        data_message.last_part = true;
        *reply = &data_message;
        *status = &c_status_ok;
        break;
    case DOS_CMD_CREATE_DIR:
        *reply = &c_message_empty;
        command->message[command->length] = 0;
        res = fm->create_dir(path, (char *) &command->message[2]);

        if (res == FR_OK) {
            *status = &c_status_ok;
        } else {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
            *status = &status_message;
        }
        break;
    case DOS_CMD_OPEN_DIR:
        cleanupDirectory();
        res = fm->get_directory(path, directoryList, NULL);
        *reply = &c_message_empty;
        if (res != FR_OK) {
            *status = &c_status_cannot_read_dir;
            break;
        }
        dir_entries = directoryList.get_elements();
        if (dir_entries == 0) {
            *status = &c_status_directory_empty;
        } else {
            *status = &c_status_ok;
        }
        break;

    case DOS_CMD_READ_DIR:
        current_index = 0;
        dos_state = e_dos_in_directory;
        get_more_data(reply, status);
        break;
    case DOS_CMD_READ_DATA:
        if (!file) {
            *reply = &c_message_empty;
            *status = &c_status_file_not_open;
        } else {
            remaining = (((int) command->message[3]) << 8)
                    | command->message[2];
            dos_state = e_dos_in_file;
            get_more_data(reply, status);
        }
        break;
    case DOS_CMD_WRITE_DATA:
        *reply = &c_message_empty;
        if (!file) {
            *status = &c_status_file_not_open;
        } else {
            res = file->write(&command->message[4], command->length - 4,
                    &transferred);
            *status = &c_status_ok;
            if (res != FR_OK) {
                strcpy((char *) status_message.message,
                        FileSystem::get_error_string(res));
                status_message.length = strlen((char *) status_message.message);
                *status = &status_message;
            }
        }
        break;
    case DOS_CMD_ECHO:
        command->last_part = true;
        *reply = command;
        *status = &c_status_ok;
        break;
    case DOS_CMD_GET_TIME: {
        int y, M, D, wd, h, m, s;
        rtc.get_time(y, M, D, wd, h, m, s);
        int extFormat = 0;
        if (command->length > 2)
            extFormat = command->message[2];
        if (extFormat == 0) {
            sprintf((char*) data_message.message,
                    "%4d/%02d/%02d %02d:%02d:%02d", 1980 + y, M, D, h, m, s);
            data_message.length = 19;
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
        } else if (extFormat == 1) {
            sprintf((char*) data_message.message,
                    "%s %4d/%02d/%02d %02d:%02d:%02d", wdnames[wd], 1980 + y, M,
                    D, h, m, s);
            data_message.length = 23;
            *status = &c_status_ok;
            data_message.last_part = true;
            *reply = &data_message;
        } else {
            *status = &c_status_unknown_command;
            *reply = &c_message_empty;
        }
        break;
    }
    case DOS_CMD_SET_TIME: {
        if (true) {
            int y, M, D, wd, h, m, s, yz, mz, cz;
            int len = command->length;
            if (len != 8) {
                sprintf((char*) data_message.message, "%02d", len);
                data_message.length = 2;

                data_message.last_part = true;
                *reply = &data_message;

                //*reply  = &c_message_empty;
                *status = &c_status_unknown_command;
            } else {
                y = command->message[2];
                M = command->message[3];
                D = command->message[4];
                h = command->message[5];
                m = command->message[6];
                s = command->message[7];
                yz = y + 1900;
                y -= 80;
                mz = M;
                if (M < 3) {
                    yz--;
                    mz += 12;
                }
                cz = yz / 100;
                yz = yz % 100;
                int wdz = D + (mz + 1) * 13 / 5 + yz + yz / 4 + cz / 4 - 2 * cz
                        + 6;
                wdz = wdz % 7;
                if (wdz < 0)
                    wdz += 7;
                int corr = rtc.get_correction();
                rtc.set_time(y, M, D, wdz, h, m, s);
                rtc.set_time_in_chip(corr, y, M, D, wdz, h, m, s);
                rtc.get_time(y, M, D, wd, h, m, s);
                sprintf((char*) data_message.message,
                        "%s %4d/%02d/%02d %02d:%02d:%02d", wdnames[wd],
                        1980 + y, M, D, h, m, s);
                data_message.length = 23;
                *status = &c_status_ok;
                data_message.last_part = true;
                *reply = &data_message;
            }
        } else {
            *status = &c_status_prohibited;
            *reply = &c_message_empty;
        }
        break;
    }
        case CTRL_CMD_LOAD_INTO_RAMDISK: {
               *reply = &c_message_empty;
               *status = &c_status_ok;
               command->message[command->length] = 0;
               
               int drvNo = command->message[2];
               bool whatIfMode = false;
               if (drvNo & 0x80)
               {
                  drvNo &= 0x7F;
                  whatIfMode = true;
               }
               
               char* filename = (char *) &command->message[3];
               FileInfo* ffi = new FileInfo(INFO_SIZE);
               FileManager *fm = FileManager :: getFileManager();
               FRESULT res = fm->fstat(path, filename, *ffi);
               if (res != FR_OK) {
                   *status = &c_status_file_not_found;
                   delete ffi;
                   break;
               }
               uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
               uint8_t ramBase = reu[0x7dc7 + drvNo];
               
               int ftype = C64 :: isMP3RamDrive(drvNo);
               // TPDO: Check extension
               
               int actType = 0;
               printf("DEBUG: Extension: %s\n", ffi->extension);
               if(strcmp(ffi->extension, "D64")==0) actType = 1541;
               if(strcmp(ffi->extension, "D71")==0) actType = 1571;
               if(strcmp(ffi->extension, "D81")==0) actType = 1581;
               if(strcmp(ffi->extension, "DNP")==0) actType = DRVTYPE_MP3_DNP;
               
               bool ok = actType == ftype;
               if (!ok)
                {
                   printf("DEBUG1: Wrong type, actual = %i, ex�ectted = %i\n", actType, ftype);
                   *status = &c_status_incompatible_image;
                   delete ffi;
                   break;
               }
               
               int expSize = 1;
               if (ftype == 1541) expSize = 174848;
               if (ftype == 1571) expSize = 2*174848;
               if (ftype == 1581) expSize = 819200;
               if (ftype == DRVTYPE_MP3_DNP)  expSize = C64 :: getSizeOfMP3NativeRamdrive(drvNo);
               int actSize = ffi->size;
               if ((expSize != actSize) && (ftype == DRVTYPE_MP3_DNP)) 
               {
                   printf("DEBUG1: Expected Size: %i, Actual Size: %i\n", expSize, actSize);
                   *status = &c_status_incompatible_image;
                   delete ffi;
                   break;
               }
               if (expSize > 12*1024*1024)
                {
                   printf("DEBUG1: Expected Size: %i too large\n", expSize);
                   *status = &c_status_incompatible_image;
                   delete ffi;
                   break;
               }

               if (ramBase > 0x40)
                {
                   *status = &c_status_incompatible_image;
                   delete ffi;
                   break;
               }
               
               if (whatIfMode)
                   break;
               
               uint8_t* dstAddr = reu+(((uint32_t) ramBase) << 16);
               // FileManager *fm = FileManager::getFileManager();
               FileInfo info(32);
               File *file = 0;
               FRESULT fres = fm->fopen(path,filename, FA_READ, &file);
               if (file) {
                  uint32_t bytes_read;
                  // total_bytes_read = 0;
                  if (ftype == 1571) {
                     file->read(dstAddr, expSize / 2, &bytes_read);
                     // total_bytes_read += bytes_read;
                     file->read(dstAddr + 700 * 256, expSize / 2, &bytes_read);
                  } else {
                     file->read(dstAddr, expSize, &bytes_read);
                  }
                  // total_bytes_read += bytes_read;
                  fm->fclose(file);
               }
               else
               {
                   *status = &c_status_internal_error;
                   delete ffi;
                   break;
               }
               
            }
            break;

        case CTRL_CMD_SAVE_RAMDISK: {
               *reply = &c_message_empty;
               *status = &c_status_ok;
               command->message[command->length] = 0;
               
               int drvNo = command->message[2];

               char* filename = (char *) &command->message[3];
               FileManager *fm = FileManager :: getFileManager();
               char extension[3];
               get_extension(filename, extension);
               
               int actType = 1;
               printf("DEBUG: Extension: %s\n", extension);
               if(strcmp(extension, "D64")==0) actType = 1541;
               if(strcmp(extension, "D71")==0) actType = 1571;
               if(strcmp(extension, "D81")==0) actType = 1581;
               if(strcmp(extension, "DNP")==0) actType = DRVTYPE_MP3_DNP;

               uint8_t* reu = (uint8_t *)(REU_MEMORY_BASE);
               uint8_t ramBase = reu[0x7dc7 + drvNo];
               
               int ftype = C64 :: isMP3RamDrive(drvNo);
               
               bool ok = actType == ftype;
               if (!ok)
                {
                   printf("DEBUG1: Wrong type, actual = %i, ex�ectted = %i\n", actType, ftype);
                   *status = &c_status_incompatible_image;
                   delete ffi;
                   break;
               }

               int expSize = 1;
               if (ftype == 1541) expSize = 174848;
               if (ftype == 1571) expSize = 2*174848;
               if (ftype == 1581) expSize = 819200;
               if (ftype == DRVTYPE_MP3_DNP)  expSize = C64 :: getSizeOfMP3NativeRamdrive(drvNo);

               if (expSize > 12*1024*1024)
                {
                   printf("DEBUG1: Expected Size: %i too large\n", expSize);
                   *status = &c_status_incompatible_image;
                   // delete ffi;
                   break;
               }

               if (ramBase > 0x40)
                {
                   *status = &c_status_incompatible_image;
                   // delete ffi;
                   break;
               }
               
               uint8_t* dstAddr = reu+(((uint32_t) ramBase) << 16);
               File *file = 0;
               FRESULT fres = fm->fopen(path,filename, FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS, &file);
               if (file) {
                  uint32_t bytes_written;
                  if (ftype == 1571) {
                     file->write(dstAddr, expSize / 2, &bytes_written);
                     // total_bytes_read += bytes_read;
                     file->write(dstAddr + 700 * 256, expSize / 2, &bytes_written);
                  } else {
                     file->write(dstAddr, expSize, &bytes_written);
                  }
                  // total_bytes_read += bytes_read;
                  fm->fclose(file);
               }
               else
               {
                   *status = &c_status_internal_error;
                   // delete ffi;
                   break;
               }
            }
            break;

    default:
        *reply = &c_message_empty;
        *status = &c_status_unknown_command;
    }
}

void Dos::cleanupDirectory() {
    for (int i = 0; i < directoryList.get_elements(); i++) {
        delete directoryList[i];
    }
    directoryList.clear_list();
}

void Dos::get_more_data(Message **reply, Message **status) {
    uint32_t transferred = 0;
    FRESULT res;
    int length;
    FileInfo *fi;

    switch (dos_state) {
    case e_dos_idle:
        printf("DOS: asking for more data, but state is idle\n");
        *reply = &c_message_empty;
        *status = &c_status_no_data;
        break;
    case e_dos_in_file:
        length = (remaining > 512) ? 512 : remaining;
        res = file->read(data_message.message, length, &transferred);
        data_message.length = (int) transferred;
        remaining -= transferred;
        if ((transferred != length) || (remaining == 0)) {
            data_message.last_part = true;
            dos_state = e_dos_idle;
        } else {
            data_message.last_part = false;
        }
        if (res != FR_OK) {
            strcpy((char *) status_message.message,
                    FileSystem::get_error_string(res));
            status_message.length = strlen((char *) status_message.message);
        } else {
            *status = &c_message_empty;
        }
        *reply = &data_message;
        break;
    case e_dos_in_directory:
        fi = directoryList[current_index];
        if (!fi) {
            *status = &c_status_internal_error;
            *reply = &c_message_empty;
        } else {
            data_message.message[0] = fi->attrib;
            strcpy((char *) &data_message.message[1], fi->lfname);
            data_message.length = 1 + strlen((char*) &data_message.message[1]);
            current_index++;
            if (current_index == dir_entries) {
                data_message.last_part = true;
                *status = &c_status_ok;
                dos_state = e_dos_idle;
            } else {
                *status = &c_message_empty;
                data_message.last_part = false;
            }
            *reply = &data_message;
        }
        break;

    default:
        printf("DOS: Illegal state\n");
        dos_state = e_dos_idle;
    }
}

void Dos::abort(int a) {
    dos_state = e_dos_idle;
}
