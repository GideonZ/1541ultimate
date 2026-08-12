/*
 * configio.h
 *
 *  Created on: Aug 22, 2019
 *      Author: gideon
 */

#ifndef CONFIGIO_H_
#define CONFIGIO_H_

#include "config.h"
#include "menu.h"
#include "filemanager.h"
#include "stream_textlog.h"

// What one "item=value" line in a .cfg file turned out to be.
//
// A line that names something this machine does not have is not an error: a
// .cfg written on a machine with different hardware is the ordinary case, and
// it warns rather than failing. It is still kept apart from an applied line,
// because a store whose items were all ignored must not be effectuated.
typedef enum {
    CFG_LINE_APPLIED = 0,   // the value was read and given to the item
    CFG_LINE_UNKNOWN,       // no such item here; warned and skipped
    CFG_LINE_MALFORMED,     // the line itself is not valid; an error
} t_cfg_line_result;

class ConfigIO : public ObjectWithMenu
{
    static void S_write_store_to_file(ConfigStore *s, File *f);
    static t_cfg_line_result S_read_store_element(ConfigStore *st, const char *line, int linenr, StreamTextLog *log);
    static SubsysResultCode_e S_reset_log(SubsysCommand *cmd);
    static SubsysResultCode_e S_save_log(SubsysCommand *cmd);

    struct {
        Action *savecfg;
        Action *savefile;
        Action *loadcfg;
        Action *factory;
        Action *clr_flash;
        Action *clear_dbg;
        Action *save_dbg;
    } myActions;
public:
    ConfigIO();
    virtual ~ConfigIO();

    void create_task_items(void);
    void update_task_items(bool writablePath);
    static SubsysResultCode_e S_save_to_file(SubsysCommand *cmd);
    static SubsysResultCode_e S_save(SubsysCommand *cmd);
    static SubsysResultCode_e S_restore(SubsysCommand *cmd);
    static SubsysResultCode_e S_reset(SubsysCommand *cmd);
    static SubsysResultCode_e S_clear(SubsysCommand *cmd);
    // Public so the host unit tests can drive the .cfg rules directly; it has
    // no state of its own and the alternative is a filesystem in the test.
    static void S_write_to_file(File *f);
    static bool S_read_from_file(File *f, StreamTextLog *log, IndexedList<ConfigStore *> &loaded_stores);
};


#endif /* CONFIGIO_H_ */
