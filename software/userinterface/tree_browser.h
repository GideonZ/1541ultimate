#ifndef TREEBROWSER_H
#define TREEBROWSER_H

#include "userinterface.h"
#include "browsable.h"
#include "ui_elements.h"
#include "observer.h"
#include "filemanager.h"

class TreeBrowserState;
class ContextMenu;
class ConfigBrowser;

#define MAX_SEARCH_LEN_TB 32

class ClipBoard
{
	mstring source_path;
	IndexedList<mstring *> source_files;
public:
	ClipBoard() : source_path(""), source_files(8, NULL) { }

	void reset(void) {
		for(int i=0;i<source_files.get_elements();i++) {
			delete source_files[i];
		}
		source_files.clear_list();
	}
	void setPath(const char *path) {
		source_path = path;
	}
	const char *getPath(void) {
		return source_path.c_str();
	}
	void addFile(const char *name) {
		mstring *newf = new mstring(name);
		source_files.append(newf);
	}
	int getNumberOfFiles(void) {
		return source_files.get_elements();
	}
	const char *getFileNameByIndex(int index) {
		mstring *s = source_files[index];
		if(s)
			return s->c_str();
		return "";
	}
};

class TreeBrowser : public UIObject
{
	void tasklist(void);
    void seek_char(int c);
    void cd_impl(const char *path);
public:
    char quick_seek_string[MAX_SEARCH_LEN_TB];
    int  quick_seek_length;
    bool allow_exit;
    bool has_path;

    FileManager *fm;
    UserInterface *user_interface;
    ObserverQueue *observerQueue;
    Browsable *root;
    Screen   *screen;
    Window   *window;
    Keyboard *keyb;
    Path 	 *path;

    TreeBrowserState *state_root;
    TreeBrowserState *state;
    ClipBoard clipboard;

    // link to temporary popup
    ContextMenu *contextMenu; // anchor for menu that pops up
    ConfigBrowser *configBrowser;

    // Member functions
    TreeBrowser(UserInterface *ui, Browsable *);
    virtual ~TreeBrowser();

    virtual void init(Screen *scr, Keyboard *k);
    virtual void redraw(void);
    virtual void deinit(void);

    virtual int poll(int);
    virtual int poll_inactive(void);
    virtual int handle_key(int);
    virtual void checkFileManagerEvent(void);

    void send_keystroke(int key) { if (keyb) keyb->push_head(key); }
    void reset_quick_seek(void);
    bool perform_quick_seek(void);
    
    void context(int);
    void task_menu(void);
    void config(void);
    void test_editor(void);
    void copy_selection(void);
    void delete_selected(void);
    void paste(void);
    void cd(const char *path);
    
    void invalidate(const void *obj);
    const char *getPath();
};
#endif
