// filemanager_service.cc - Refactored FileManager using a FreeRTOS service model

#include "filemanager.h"
#include "filemanager_service.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"


void FileManager::FileManagerServiceTask(void* param) {
    FileCommand *cmd;
    FileManager* fm = FileManager::getFileManager();

    for (;;) {
        if (xQueueReceive(fm->fileCommandQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd->type) {
                case FileOpType::OPEN:
                    cmd->result = fm->priv_open(cmd->path_obj, cmd->path, cmd->filename, cmd->flags, cmd->file_ptr);
                    break;
                case FileOpType::CLOSE:
                    cmd->result = fm->priv_close(*cmd->file_ptr);
                    break;
                case FileOpType::SYNC:
                    cmd->result = fm->priv_sync(*cmd->file_ptr);
                    break;
                case FileOpType::DELETE:
                    cmd->result = fm->priv_delete(cmd->path_obj, cmd->path, cmd->filename);
                    break;
                case FileOpType::RENAME:
                    cmd->result = fm->priv_rename(cmd->path, cmd->new_name);
                    break;
                case FileOpType::POLL_MEDIA:
                    // Custom media check implementation here
                    cmd->result = FR_OK;
                    break;
                case FileOpType::GET_FREE:
                    cmd->result = fm->priv_get_free(cmd->path_obj, *cmd->transferred, cmd->size);
                    break;
                case FileOpType::FS_READ_SECTOR:
                    cmd->result = fm->priv_fs_read_sector(cmd->path_obj, cmd->buffer, cmd->track, cmd->sector);
                    break;
                case FileOpType::FS_WRITE_SECTOR:
                    cmd->result = fm->priv_fs_write_sector(cmd->path_obj, cmd->buffer, cmd->track, cmd->sector);
                    break;
                case FileOpType::FSTAT:
                    cmd->result = fm->priv_stat(cmd->path_obj, cmd->path, cmd->filename, *cmd->file_info, cmd->open_mount);
                    printf("FSTAT: %b %s %p\n", cmd->result, FileSystem::get_error_string(cmd->result), cmd->file_info->fs);
                    break;
                case FileOpType::CREATE_DIR:
                    cmd->result = fm->priv_create_dir(cmd->path_obj, cmd->path, cmd->filename);
                    break;
                case FileOpType::OPEN_DIR:
                    cmd->result = fm->priv_open_dir(cmd->path, cmd->dir);
                    break;
                case FileOpType::READ_DIR:
                    cmd->result = fm->priv_read_dir(*cmd->dir, *cmd->file_info);
                    break;
                case FileOpType::CLOSE_DIR:
                    cmd->result = fm->priv_close_dir(*cmd->dir);
                    break;
                case FileOpType::READ:
                    cmd->result = fm->priv_read(*cmd->file_ptr, cmd->buffer, cmd->size, cmd->transferred);
                    break;
                case FileOpType::WRITE: 
                    cmd->result = fm->priv_write(*cmd->file_ptr, cmd->buffer, cmd->size, cmd->transferred);
                    break;
                case FileOpType::SEEK: 
                    cmd->result = fm->priv_seek(*cmd->file_ptr, cmd->size);
                    break;
                case FileOpType::IS_WRITEABLE:
                    cmd->result = fm->priv_is_path_writable(cmd->path_obj);
                    break;
                default:
                    cmd->result = FR_INVALID_PARAMETER;
                    break;
            }
            if (cmd->caller) {
                xTaskNotifyGive(cmd->caller);
            }
        }
    }
}

