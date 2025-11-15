/*
##########################################################################
##                                                                      ##
##   RENE GARCIA                        GNU General Public Licence V3   ##
##                                                                      ##
##########################################################################
##                                                                      ##
##      Project : MPS Emulator                                          ##
##      Class   : IecPrinter                                            ##
##      Piece   : iec_printer.cc                                        ##
##      Language: C ANSI                                                ##
##      Author  : Rene GARCIA                                           ##
##                                                                      ##
##########################################################################
*/

        /*-
         *
         *  $Id:$
         *
         *  $URL:$
         *
        -*/

/******************************  Inclusions  ****************************/

#include "iec_printer.h"
#include "endianness.h"
#include "init_function.h"

/******************************  Definitions  ***************************/

#define CFG_PRINTER_ID         0x30
#define CFG_PRINTER_FILENAME   0x31
#define CFG_PRINTER_TYPE       0x32
#define CFG_PRINTER_DENSITY    0x33
#define CFG_PRINTER_EMULATION  0x34
#define CFG_PRINTER_CBM_CHAR   0x35
#define CFG_PRINTER_EPSON_CHAR 0x36
#define CFG_PRINTER_IBM_CHAR   0x37
#define CFG_PRINTER_ENABLE     0x38
#define CFG_PRINTER_PAGETOP    0x39
#define CFG_PRINTER_PAGEHEIGHT 0x3A

#define MENU_PRINTER_ON        0x1230
#define MENU_PRINTER_OFF       0x1231
#define MENU_PRINTER_FLUSH     0x1232
#define MENU_PRINTER_RESET     0x1233

/************************************************************************
*                                                                       *
*               G L O B A L   M O D U L E   V A R I A B L E S           *
*                                                                       *
************************************************************************/

/* =======  Lookup table for ASCII output (ISO 8859-1 to keep pound sign) */
uint8_t IecPrinter::ascii_lut[256] =
{//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
     0,  0,  0,  0,  0,  0,  0,  0,  0,  9, 10,  0,  0, 13,  0,  0, // 0
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 1
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, // 2
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, // 3
    64, 97, 98, 99,100,101,102,103,104,105,106,107,108,109,110,111, // 4
   112,113,114,115,116,117,118,119,120,121,122, 91,163, 93, 94, 95, // 5
    45, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, // 6
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 43, 32,124, 32, 32, // 7
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 8
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 9
    32, 32, 32, 45, 45,124, 32, 32, 32, 32,124, 43, 32, 43, 43, 32, // A
    43, 43, 43, 43,124, 32, 32, 45, 32, 32, 32, 32, 32, 43, 32, 32, // B
    45, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, // C
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 43, 32,124, 32, 32, // D
    32, 32, 32, 45, 45,124, 32, 32, 32, 32,124, 43, 32, 43, 43, 32, // E
    43, 43, 43, 43,124, 32, 32, 45, 32, 32, 32, 32, 32, 43, 32, 32  // F
};

/* =======  User interface menu items */

static const char *pr_typ[] = { "RAW", "ASCII", "PNG B&W", "PNG COLOR" };
static const char *pr_ink[] = { "Low", "Medium", "High", "Squares" };
static const char *pr_emu[] = { "Commodore MPS", "Epson FX-80/JX-80", "IBM Graphics Printer", "IBM Proprinter" };
static const char *pr_cch[] = { "USA/UK", "Denmark", "France/Italy", "Germany", "Spain", "Sweden", "Switzerland" };
static const char *pr_ech[] = { "Basic", "USA", "France", "Germany", "UK", "Denmark I",
                                "Sweden", "Italy", "Spain", "Japan", "Norway", "Denmark II" };
static const char *pr_ich[] = { "International 1", "International 2", "Israel", "Greece", "Portugal", "Spain" };

static struct t_cfg_definition iec_printer_config[] = {
    { CFG_PRINTER_ENABLE,     CFG_TYPE_ENUM,   "IEC printer",       "%s", en_dis, 0,  1, 0 },
    { CFG_PRINTER_ID,         CFG_TYPE_VALUE,  "Bus ID",            "%d", NULL,   4,  5, 4 },
    { CFG_PRINTER_FILENAME,   CFG_TYPE_STRING, "Output file",       "%s", NULL,   1, 31, (int) FS_ROOT "printer" },
    { CFG_PRINTER_TYPE,       CFG_TYPE_ENUM,   "Output type",       "%s", pr_typ, 0,  3, 2 },
    { CFG_PRINTER_DENSITY,    CFG_TYPE_ENUM,   "Ink density",       "%s", pr_ink, 0,  3, 1 },
    { CFG_PRINTER_PAGETOP,    CFG_TYPE_VALUE,  "Page top margin (default is 5)", "%d", NULL, 0, IEC_PRINTER_PAGE_LINES-1, 5 },
    { CFG_PRINTER_PAGEHEIGHT, CFG_TYPE_VALUE,  "Page height (default is 60)",    "%d", NULL, 1, IEC_PRINTER_PAGE_LINES+1, 60 },
    { CFG_PRINTER_EMULATION,  CFG_TYPE_ENUM,   "Emulation",         "%s", pr_emu, 0,  3, 0 },
    { CFG_PRINTER_CBM_CHAR,   CFG_TYPE_ENUM,   "Commodore charset", "%s", pr_cch, 0,  6, 0 },
    { CFG_PRINTER_EPSON_CHAR, CFG_TYPE_ENUM,   "Epson charset",     "%s", pr_ech, 0, 11, 0 },
    { CFG_PRINTER_IBM_CHAR,   CFG_TYPE_ENUM,   "IBM table 2",       "%s", pr_ich, 0,  5, 0 },
    { 0xFF,                   CFG_TYPE_END,    "",                  "",   NULL,   0,  0, 0 }
};

/* =======  Subsystem creation and registration */

/* This is the pointer to the virtual printer */
IecPrinter *iec_printer = NULL;

/* This will create the IecPrinter object */
static void init_printer(void *_a, void *_b)
{
    iec_printer = new IecPrinter();
}

/*-
 *
 *  This global will cause us to run!
 *
-*/
InitFunction iec_printer_init("Printer", init_printer, NULL, NULL, 12);

/************************************************************************
*                  IecPrinter::effectuate_settings()            Public  *
*                  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Load and apply virtual printer configuration from nvram    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (none)                                                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

void IecPrinter::effectuate_settings(void)
{
    /* Page top margin and printable height are configurable */
    uint8_t page_top, page_height;

    /* Printer ID on IEC from nvram */
    printer_iec_addr = cfg->get_value(CFG_PRINTER_ID);
    iec_if->configure();

    /* Load and apply virtual printer configuration from nvram */
    set_filename(cfg->get_string(CFG_PRINTER_FILENAME));
    set_output_type(cfg->get_value(CFG_PRINTER_TYPE));
    set_ink_density(cfg->get_value(CFG_PRINTER_DENSITY));
    set_emulation(cfg->get_value(CFG_PRINTER_EMULATION));
    set_cbm_charset(cfg->get_value(CFG_PRINTER_CBM_CHAR));
    set_epson_charset(cfg->get_value(CFG_PRINTER_EPSON_CHAR));
    set_ibm_charset(cfg->get_value(CFG_PRINTER_IBM_CHAR));

    /* Configure printable area according to user settings */
    page_top = cfg->get_value(CFG_PRINTER_PAGETOP);
    page_height = cfg->get_value(CFG_PRINTER_PAGEHEIGHT);

    /* If printable area is bigger than page, reduce it */
    if ((page_top+page_height) > IEC_PRINTER_PAGE_LINES+1)
    {
        page_height = IEC_PRINTER_PAGE_LINES + 1 - page_top;
        cfg->set_value(CFG_PRINTER_PAGEHEIGHT, page_height);
    }

    set_page_top(page_top);
    set_page_height(page_height);

    /* Enable printer if requested */
    printer_enable = uint8_t(cfg->get_value(CFG_PRINTER_ENABLE));
}

/************************************************************************
*                    IecPrinter::create_task_items()            Public  *
*                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Create context F5 menu items for the virtual printer tasks *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (none)                                                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

void IecPrinter::create_task_items(void)
{
    /* Create items */
    myActions.eject    = new Action("Flush/Eject", SUBSYSID_PRINTER, MENU_PRINTER_FLUSH);
    myActions.reset    = new Action("Reset",       SUBSYSID_PRINTER, MENU_PRINTER_RESET);
    myActions.turn_on  = new Action("Turn On",     SUBSYSID_PRINTER, MENU_PRINTER_ON);
    myActions.turn_off = new Action("Turn Off",    SUBSYSID_PRINTER, MENU_PRINTER_OFF);

    /* Create collection and attach items */
    TaskCategory *prt = TasksCollection :: getCategory("Printer", SORT_ORDER_PRINTER);
    prt->append(myActions.eject);
    prt->append(myActions.reset);
    prt->append(myActions.turn_on);
    prt->append(myActions.turn_off);
}

/************************************************************************
*              IecPrinter::update_task_items(writable,path)     Public  *
*              ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~             *
* Function : Update context F5 menu items for the virtual printer tasks *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    writable : (bool) ignored                                          *
*    path     : (Path *) ignored                                        *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

void IecPrinter::update_task_items(bool writablePath)
{
    if (printer_enable) {
        myActions.turn_off->show();
        myActions.turn_on->hide();
        myActions.eject->enable();
        myActions.reset->enable();
    } else {
        myActions.turn_off->hide();
        myActions.turn_on->show();
        myActions.eject->disable();
        myActions.reset->disable();
    }
}

/************************************************************************
*                     IecPrinter::executeCommand(cmd)           Public  *
*                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Callback called when user selects one task menu item       *
*            called from GUI task                                       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    cmd : (SubsysCommand *) item selected by user                      *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (int) always 0                                                     *
*                                                                       *
************************************************************************/

SubsysResultCode_e IecPrinter::executeCommand(SubsysCommand *cmd)
{
    PrinterEvent_t printerEvent;  // event to send to printer task

    /* Record user_interface for modal popup */
    cmd_ui = cmd->user_interface;

    switch(cmd->functionID)
    {
        case MENU_PRINTER_ON:
            reset();
            printer_enable = 1;
            cfg->set_value(CFG_PRINTER_ENABLE, printer_enable);
            iec_if->configure();
            break;

        case MENU_PRINTER_OFF:
            printer_enable = 0;
            cfg->set_value(CFG_PRINTER_ENABLE, printer_enable);
            iec_if->configure();
            break;

        case MENU_PRINTER_FLUSH:

            /*-
             *  Modal dialog while printing
             *
             *  a progress bar shows how manu blocs have been compressed in deflate method
             *  the total number of blocs depends on data size to compress, 8 blocs for
             *  B&W printer pages and 20 blocs for color pages
            -*/

            cmd_ui->show_progress("Flushing/Ejecting page...", is_color ? 20 : 8);
            printerEvent.type = PRINTER_EVENT_USER;
            printerEvent.value = PRINTER_USERCMD_FLUSH;
            while( xQueueSend( queueHandle, (void *) &printerEvent, portMAX_DELAY) != pdTRUE);
            xSemaphoreTake( xCallerSemaphore, portMAX_DELAY); /* Wait end of printer job */
            cmd_ui->hide_progress();
            break;

        case MENU_PRINTER_RESET:
            printerEvent.type = PRINTER_EVENT_USER;
            printerEvent.value = PRINTER_USERCMD_RESET;
            while( xQueueSend( queueHandle, (void *) &printerEvent, portMAX_DELAY) != pdTRUE);
            break;

        default:
            break;
    }

    cmd_ui = NULL;

    return SSRET_OK;
}

/************************************************************************
*                 IecPrinter::updateFlushProgressBar()          Public  *
*                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Callback called when one bloc has been compressed in PNG   *
*            generation routine. It updates the progress bar when eject *
*            has been started by the Action menu                        *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (none)                                                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

void IecPrinter::updateFlushProgressBar(void)
{
    if (cmd_ui)
    {
        /* One step ahead for progress bar in interactive mode only */
        cmd_ui->update_progress(NULL, 1);
    }
}

/************************************************************************
*                         IecPrinter::IecPrinter()         Constructor  *
*                         ~~~~~~~~~~~~~~~~~~~~~~~~                      *
* Function : Instanciate one IecPrinter object                          *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (none)                                                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

IecPrinter::IecPrinter() : SubSystem(SUBSYSID_PRINTER)
{
    fm = FileManager::getFileManager();

    /* Create printer settings page in F2 menu */
    register_store(0x4d505300, "Printer Settings", iec_printer_config);

    /* Initial values */
    output_filename = NULL;
    f = NULL;
    printer_iec_addr = 4;
    mps = MpsPrinter::getMpsPrinter();
    buffer_pointer = 0;
    output_type = PRINTER_UNSET_OUTPUT;
    cmd_ui = 0;
    is_color = false;

    /* Read configuration from nvram if it exists */
    effectuate_settings();

    /* Register printer with the interface */
    IecInterface *interface = IecInterface :: get_iec_interface();
    slot_id = interface->register_slave(this);
    interface->configure();

    /* Initially, no caller task */
    xCallerSemaphore = xSemaphoreCreateBinary();

    /* Create the queue */
    queueHandle = xQueueCreate( 1, sizeof(PrinterEvent_t));

    /* Create the task, storing the handle. */
    xTaskCreate((TaskFunction_t) IecPrinter::task, "Virtual Printer",
                configMINIMAL_STACK_SIZE, this,
                PRIO_BACKGROUND, &taskHandle);
}

/************************************************************************
*                         IecPrinter::~IecPrinter()         Destructor  *
*                         ~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Delete a IecPrinter object                                 *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (none)                                                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (none)                                                             *
*                                                                       *
************************************************************************/

IecPrinter::~IecPrinter()
{
    /* Remove task from process list */
    vTaskDelete(taskHandle);

    /* Remove message queue */
    vQueueDelete(queueHandle);

    /* Remove semaphore */
    vSemaphoreDelete( xCallerSemaphore );
    /* Close/flush output file */
    close_file();
}

/************************************************************************
*                       IecPrinter::push_data(b)                Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~                        *
* Function : Interpret one data byte received by data IEC channel       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received data byte                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

t_channel_retval IecPrinter::push_data(uint8_t b)
{
    PrinterEvent_t printerEvent;

    printerEvent.type = PRINTER_EVENT_DATA;
    printerEvent.value = b;

    while( xQueueSend( queueHandle, (void *) &printerEvent, portMAX_DELAY) != pdTRUE);

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::_push_data(b)              Private  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~                       *
* Function : Interpret one data byte received by data IEC channel       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received data byte                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK on success or IEC_BYTE_LOST if an error occured             *
*                                                                       *
************************************************************************/

t_channel_retval IecPrinter::_push_data(uint8_t b)
{
    if (output_type == PRINTER_ASCII_OUTPUT)
    {
        if ((buffer[buffer_pointer] = ascii_lut[b]))
            buffer_pointer++;
    }
    else
    {
        buffer[buffer_pointer++] = b;
    }

    if(buffer_pointer == IEC_PRINTER_BUFFERSIZE)
    {
        switch (output_type)
        {
            case PRINTER_PNG_OUTPUT:
                mps->Interpreter(buffer,IEC_PRINTER_BUFFERSIZE);
                buffer_pointer = 0;
                break;

            case PRINTER_RAW_OUTPUT:
            case PRINTER_ASCII_OUTPUT:
                if(!f)
                    open_file();

                if (f)
                {
                    uint32_t bytes;
                    f->write(buffer, IEC_PRINTER_BUFFERSIZE, &bytes);
                    buffer_pointer = 0;
                }
                else
                {
                    buffer_pointer--;
                    return IEC_BYTE_LOST;
                }

                break;
        }
    }

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::push_command(b)             Public  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Interpret one data byte received by IEC command channel    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received command byte                                *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

t_channel_retval IecPrinter::push_ctrl(uint16_t b)
{
    PrinterEvent_t printerEvent;

    printerEvent.type = PRINTER_EVENT_CMD;
    switch(b) {
        case SLAVE_CMD_EOI:
            printerEvent.value = 0xFF;
            break;
        case SLAVE_CMD_ATN:
            printerEvent.value = 0xFE;
            break;
        default:
            printerEvent.value = (uint8_t)(b & 7);
    }

    while( xQueueSend( queueHandle, (void *) &printerEvent, portMAX_DELAY) != pdTRUE);

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::push_command(b)            Private  *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Interpret one data byte received by IEC command channel    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    b : (uint8_t) received command byte                                *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

t_channel_retval IecPrinter::_push_command(uint8_t b)
{
    switch(b) {
        case 0xFE: // Received printer OPEN
        case 0x00: // CURSOR UP (graphics/upper case) mode (default)
            if (output_type == PRINTER_PNG_OUTPUT) {
                if (buffer_pointer)
                {
                    mps->Interpreter(buffer,buffer_pointer);
                    buffer_pointer=0;
                }
                mps->setCharsetVariant(0);
            }

            break;

        case 0x07: // CURSOR DOWN (upper case/lower case) mode
            if (output_type == PRINTER_PNG_OUTPUT) {
                if (buffer_pointer)
                {
                    mps->Interpreter(buffer,buffer_pointer);
                    buffer_pointer=0;
                }
                mps->setCharsetVariant(1);
            }

            break;

        case 0xFF: // Received EOI (Close file)
            if (output_type != PRINTER_PNG_OUTPUT) close_file();
            break;
    }

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::flush()                 Pubic   *
*                           ~~~~~~~~~~~~~~~~~~~                         *
* Function : Force remaining bytes in printer buffer to be output to    *
*            file in RAW and ASCII output mode                          *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::flush(void)
{
    if (buffer_pointer && output_type != PRINTER_PNG_OUTPUT && !f)
        open_file();

    close_file();

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::reset()                 Pubic   *
*                           ~~~~~~~~~~~~~~~~~~~                         *
* Function : Reset printer emulation settings to defaults               *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::_reset(void)
{
    buffer_pointer = 0;
    mps->Reset();

    return IEC_OK;
}

/************************************************************************
*                                   task()                      Static  *
*                                   ~~~~~~                              *
* Function : Low priority iec printer task. Notice that task is loaded  *
*            even if printer is disabled. It will not run at all.       *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    (IecPrinter *) pointer to object printer that ccreated this task   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    none                                                               *
*                                                                       *
************************************************************************/

void IecPrinter::task(IecPrinter *p)
{
    PrinterEvent_t printerEvent;

    for(;;)
    {
        if( xQueueReceive( p->queueHandle, &printerEvent,
                           portMAX_DELAY ) == pdTRUE )
        {
            switch (printerEvent.type)
            {
                case PRINTER_EVENT_CMD:
                    p->_push_command(printerEvent.value);
                    break;

                case PRINTER_EVENT_DATA:
                    p->_push_data(printerEvent.value);
                    break;

                case PRINTER_EVENT_USER:
                    switch (printerEvent.value)
                    {
                        case PRINTER_USERCMD_RESET:
                            p->_reset();
                            iec_if->configure();
                            break;

                        case PRINTER_USERCMD_FLUSH:
                            p->flush();
                            xSemaphoreGive(p->xCallerSemaphore);
                            break;
                    }
                    break;
            }
        }
    }
}

/************************************************************************
*                     IecPrinter::set_filename(file)            Pubic   *
*                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Set output filename for printer                            *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    file: (char *) filename (with absolute path)                       *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_filename(const char *file)
{
    output_filename = file;
    mps->setFilename((char *)output_filename);

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::set_page_top(d)             Pubic   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Set printer page top margin (in text lines)                *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) from 0 to 69                                              *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    Always IEC_OK                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_page_top(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setTopMargin(d * IEC_PRINTER_PIXLINE);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_page_height(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Set printable page height (in text lines)                  *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) from 1 to 70                                              *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    Always IEC_OK                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_page_height(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setPrintableHeight(d * IEC_PRINTER_PIXLINE);

    return IEC_OK;
}

/************************************************************************
*                       IecPrinter::set_emulation(d)            Pubic   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Set printer emulation (not RAW or ASCII)                   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) Emulated printer to enable                                *
*         0 = Commodore MPS printer                                     *
*         1 = Epson FX-80 printer                                       *
*         2 = IBM Graphics Printer                                      *
*         3 = IBM Proprinter                                            *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_emulation(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    switch (d)
    {
        case 0: mps->setInterpreter(MPS_PRINTER_INTERPRETER_CBM); break;
        case 1: mps->setInterpreter(MPS_PRINTER_INTERPRETER_EPSONFX80); break;
        case 2: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMGP); break;
        case 3: mps->setInterpreter(MPS_PRINTER_INTERPRETER_IBMPP); break;
    }

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_ink_density(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Set ink density for printer emulation                      *
*            flush print buffer if not empty before changing density    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) ink density                                               *
*         0 = low                                                       *
*         1 = medium                                                    *
*         2 = high                                                      *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_ink_density(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setDotSize(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_cbm_charset(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change commodore charset for MPS printer emulation         *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = USA/UK                  4 = Spain                         *
*         1 = Denmark                 5 = Sweden                        *
*         2 = France/Italy            6 = Switzerland                   *
*         3 = Germany                                                   *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_cbm_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setCBMCharset(d);

    return IEC_OK;
}

/************************************************************************
*                     IecPrinter::set_epson_charset(d)          Pubic   *
*                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                  *
* Function : Change epson charset for FX-80 printer emulation           *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = Basic                  6 = Sweden                         *
*         1 = USA                    7 = Italy                          *
*         2 = France                 8 = Spain                          *
*         3 = Germany                9 = Japan                          *
*         4 = UK                    10 = Norway                         *
*         5 = Denmark I             11 = Denmark II                     *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_epson_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setEpsonCharset(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_ibm_charset(d)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change ibm charset for GP and PP printer emulation         *
*            flush print buffer if not empty before changing charset    *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = International 1           3 = Greece                      *
*         1 = International 2           4 = Portugal                    *
*         2 = Israel                    5 = Spain                       *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_ibm_charset(int d)
{
    if (output_type == PRINTER_PNG_OUTPUT && buffer_pointer)
    {
        mps->Interpreter(buffer,buffer_pointer);
        buffer_pointer=0;
    }

    mps->setIBMCharset(d);

    return IEC_OK;
}

/************************************************************************
*                      IecPrinter::set_output_type(t)           Pubic   *
*                      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                   *
* Function : Change virtual printer output type                         *
*            flush print buffer if type is different from current one   *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    d: (int) charset                                                   *
*         0 = RAW                                                       *
*         1 = ASCII                                                     *
*         2 = PNG (greyscale printer emulation)                         *
*         3 = PNG (color printer emulation)                             *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    IEC_OK always                                                      *
*                                                                       *
************************************************************************/

int IecPrinter::set_output_type(int t)
{
    t_printer_output_type new_output_type = output_type;

    switch (t)
    {
        case 0: // RAW output format
            new_output_type = PRINTER_RAW_OUTPUT;
            break;

        case 1: // ASCII output format
            new_output_type = PRINTER_ASCII_OUTPUT;
            break;

        case 2: // PNG grey output format
            new_output_type = PRINTER_PNG_OUTPUT;
            mps->setColorMode(is_color=false);
            break;

        case 3: // PNG color output format
            new_output_type = PRINTER_PNG_OUTPUT;
            mps->setColorMode(is_color=true);
            break;
    }

    // Don't close file on first selection (from nvram or default)
    if ((output_type != PRINTER_UNSET_OUTPUT) && (new_output_type != output_type))
        close_file();

    output_type = new_output_type;

    return IEC_OK;
}

/************************************************************************
*                           IecPrinter::open_file()           Private   *
*                           ~~~~~~~~~~~~~~~~~~~~~~~                     *
* Function : Open file to write output data                             *
*            append to existing file or create file if not existing     *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (int) 0 on success, other value on failure                         *
*                                                                       *
************************************************************************/

int IecPrinter::open_file(void)
{
    char filename[40];

    /* Add .txt extension if ASCII output type */
    sprintf(filename,(output_type == PRINTER_ASCII_OUTPUT) ? "%s.txt" : "%s", output_filename);

    FRESULT fres = fm->fopen((const char *) NULL, filename, FA_WRITE|FA_OPEN_ALWAYS, &f);

    if(f)
    {
        printf("Successfully opened printer file %s\n", filename);
        f->seek(f->get_size());
    }
    else
    {
        FRESULT fres = fm->fopen((const char *) filename, FA_WRITE|FA_CREATE_NEW, &f);

        if(f)
        {
            printf("Successfully created printer file %s\n", filename);
        }
        else
        {
            printf("Can't open printer file %s: %s\n", filename, FileSystem :: get_error_string(fres));
            return 1;
        }
    }

    return 0;
}

/************************************************************************
*                           IecPrinter::close_file()          Private   *
*                           ~~~~~~~~~~~~~~~~~~~~~~~~                    *
* Function : Close output file (and flush data in printer buffer before *
*            closing file)                                              *
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    none                                                               *
*                                                                       *
*-----------------------------------------------------------------------*
* Outputs:                                                              *
*                                                                       *
*    (int) 0 always                                                     *
*                                                                       *
************************************************************************/

int IecPrinter::close_file(void) // file should be open
{
    switch (output_type)
    {
        case PRINTER_PNG_OUTPUT:
            if (buffer_pointer > 0)
            {
                mps->Interpreter(buffer, buffer_pointer);
                buffer_pointer=0;
            }

            mps->FormFeed();

            break;

        case PRINTER_RAW_OUTPUT:
        case PRINTER_ASCII_OUTPUT:
            if(f)
            {
                if (buffer_pointer > 0)
                {
                    uint32_t bytes;

                    f->write(buffer, buffer_pointer, &bytes);
                    buffer_pointer = 0;
                }

                fm->fclose(f);
                f = NULL;
            }

            break;
    }

    return 0;
}

/************************************************************************
*                       IecPrinter::info(JSON_Object *)        Public   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                 *
* Function: Provide information about the printer in JSON format        * 
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    j : JSON Object. This is the entry that belongs to the device on   *
*        the bus. We can add custom properties here.                    *
*                                                                       *
************************************************************************/
void IecPrinter :: info(JSON_Object *_j)
{
    // Add JSON entries to the object
    // j->add("test", false);
}

/************************************************************************
*                       IecPrinter::info(StreamTextLog&)       Public   *
*                       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                *
* Function: Provide information about the printer for SystemInfo screen * 
*-----------------------------------------------------------------------*
* Inputs:                                                               *
*                                                                       *
*    log : Log Object to which we can add text in ASCII format          *
*          Indent should be 4.                                          *
*                                                                       *
************************************************************************/
void IecPrinter :: info(StreamTextLog& _log)
{
    // BusID and Enable have already been printed, so we can simply print more info about charset etc.
    // log.format("    TestInfo from Printer.\n");
}

/****************************  END OF FILE  ****************************/
