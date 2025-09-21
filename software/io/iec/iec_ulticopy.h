#ifndef ULTICOPY_H
#define ULTICOPY_H

#include "iec_interface.h"
#include "filemanager.h"
#include "menu.h"
#include "subsys.h"
#include "userinterface.h"
#include "disk_image.h"

//#include "fs_errors_flags.h"
//#include "stream_textlog.h"
class UltiCopyWindow;


class UltiCopy : public ObjectWithMenu, SubSystem
{
    FileManager *fm;
    IecInterface *intf;
    UserInterface *cmd_ui;
    SemaphoreHandle_t ulticopyBusy;
    SemaphoreHandle_t ulticopyMutex;
    Path *path;

    bool wait_irq;
    int last_track;
    int warp_drive;
    uint8_t warp_return_code;

    BinImage *ulticopy_bin_image;

    UltiCopyWindow *ui_window;

    struct {
        Action *ulticopy8;
        Action *ulticopy9;
        Action *ulticopy10;
        Action *ulticopy11;
    } myActions;

    // Local Functionality
    static void S_warp_loop(IecSlave *sl, void *data);
    void warp_loop(void);
    void start_warp(int);
    bool start_warp_iec(void);
    void get_warp_data(void);
    void get_warp_error(void);
    void save_copied_disk(void);
public:
    UltiCopy();
    ~UltiCopy();
    
    // Subsys (called from GUI task)
    SubsysResultCode_e executeCommand(SubsysCommand *cmd); // from SubSystem
    const char *identify(void) { return "UltiCopy"; }

    // ObjectWithMenu (called from GUI task)
    void create_task_items(void);
    void update_task_items(bool writablePath, Path *path);
};

class UltiCopyWindow : public UIObject
{
public:
    Screen   *parent_win;
    Window   *window;
    Keyboard *keyb;
    int return_code;

    // Member functions
    UltiCopyWindow(UserInterface *ui);
    virtual ~UltiCopyWindow();

    virtual void init();
    virtual void deinit(void);

    virtual int poll(int);
    virtual int handle_key(uint8_t);

    void close(void);
};

#endif
