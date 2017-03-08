/* ========================================================================== */
/*                                                                            */
/*   TreeBrowser.cc                                                           */
/*   (c) 2010 Gideon Zweijtzer                                                */
/*                                                                            */
/* ========================================================================== */
#include "tree_browser.h"
#include "tree_browser_state.h"
#include "config_menu.h"
#include "context_menu.h"
#include "task_menu.h"
#include "filemanager.h"
#include "editor.h"
#include "userinterface.h"
#include "browsable_root.h"
#include "keyboard_usb.h"

static const char *helptext=
		"CRSR UP/DN: Selection up/down\n"
		"CRSR LEFT:  Go one level up\n"
		"            leave directory or disk\n"
		"CRSR RIGHT: Go one level down\n"
		"            enter directory or disk\n"
		"RETURN:     Selection context menu\n"
		"RUN/STOP:   Leave menu / Back\n"
		"\n"
		"F1:         Selection Page up\n"
		"F7:         Selection Page down\n"
		"\n"
		"F2:         Enter the setup menu\n"
		"F5:         Action menu\n"
		"\n"
		"SPACE:      Select file / directory\n"
		"C=-A        Select all\n"
		"C=-N        Deselect all\n"
		"C=-C        Copy current selection\n"
		"C=-V        Paste selection here.\n"
		"\n"
		"Quick seek: Use the keyboard to type\n"
		"            the name to search for.\n"
		"            You can use ? as a\n"
		"            wildcard.\n"
		"\n"
		"F6:         Show debug log\n"
		"\nRUN/STOP to close this window.";

#include "stream_textlog.h"
extern StreamTextLog textLog; // the global log

/***********************/
/* Tree Browser Object */
/***********************/
TreeBrowser :: TreeBrowser(UserInterface *ui, Browsable *root)
{
	// initialize state
	user_interface = ui;
	window = NULL;
    keyb = NULL;
    contextMenu = NULL;
    configBrowser = NULL;
    quick_seek_length = 0;
    quick_seek_string[0] = '\0';
    this->root = root;
    state = 0;
    state_root = 0;
    fm = FileManager :: getFileManager();
    path = fm->get_new_path("Tree Browser");
    observerQueue = new ObserverQueue();
    fm->registerObserver(observerQueue);
}

TreeBrowser :: ~TreeBrowser()
{
	if(state)
		delete state;
	FileManager :: getFileManager() -> release_path(path);
	if (observerQueue) {
	    fm->deregisterObserver(observerQueue);
		delete observerQueue;
	}
}

void TreeBrowser :: init(Screen *screen, Keyboard *k) // call on root!
{
	this->screen = screen;

    screen->move_cursor(screen->get_size_x()-8, screen->get_size_y()-1);
	screen->output("\eAF3=Help\eO");

	window = new Window(screen, 0, 2, screen->get_size_x(), screen->get_size_y()-3);
	keyb = k;
	if(!state) {
		state = new TreeBrowserState(root, this, 0);
		state_root = state;
	}
    state->reload();
	// state->do_refresh();
}

void TreeBrowser :: deinit(void)
{
	if(window) {
		delete window;
		window = NULL;
	}
}

void TreeBrowser :: config(void)
{
    printf("Creating config menu...\n");
        
    Browsable *configRoot = new BrowsableConfigRoot();
    configBrowser = new ConfigBrowser(user_interface, configRoot);
    configBrowser->init(screen, keyb);
    state->refresh = true; // refresh as soon as we come back
    user_interface->activate_uiobject(configBrowser);

    // from this moment on, we loose focus.. polls will go directly to config menu!
}

void TreeBrowser :: context(int initial)
{
	if(!state->under_cursor)
		return;

    //printf("Creating context menu for %s\n", state->under_cursor->getName());
    contextMenu = new ContextMenu(user_interface, state, initial, state->selected_line);
    contextMenu->init(window, keyb);
    user_interface->activate_uiobject(contextMenu);
    // from this moment on, we loose focus.. polls will go directly to context menu!
}

void TreeBrowser :: task_menu(void)
{
	if(!state->node)
		return;
    //printf("Creating task menu for %s\n", state->node->getName());
    contextMenu = new TaskMenu(user_interface, state, path);
    contextMenu->init(window, keyb);
    user_interface->activate_uiobject(contextMenu);
    // from this moment on, we loose focus.. polls will go directly to menu!
}

void TreeBrowser :: test_editor(void)
{
    Editor *edit = new Editor(user_interface, NULL); // use built-in text
    edit->init(screen, keyb);
    int ret;
    do {
        ret = edit->poll(0);
    } while(!ret);
    edit->deinit();
}
    

int TreeBrowser :: poll(int sub_returned)
{
	int c;
    int ret = 0;

    if(contextMenu) {
        if(sub_returned < 0) {
        	delete contextMenu;
            contextMenu = NULL;
            state->draw();
        } else if(sub_returned > 0) {
            // printf("Pointer to selected context/menu item: %p\n", menu_browser->state->selected);
            // 0 is dummy, bec it is of the type ConfigItem. ConfigItem
            // itself knows the value that the actual object (= selected of THIS
            // browser!) needs to be called with. It would be better to just
            // create a return value of a GUI object, and call execute
            // with that immediately.
            state->draw();
            contextMenu->executeAction();
            delete contextMenu;
            contextMenu = NULL;
            state->draw();
        }
        return ret;
    }

    checkFileManagerEvent();

    if(state->refresh) {
        state->do_refresh();
	}

    c = keyb->getch();
    if(c == -2) { // error
        printf("Keyboard returned -2\n");
        return -2;
    }
    if(c >= 0) {
    	ret = handle_key(c);
    }
	return ret;
}

void TreeBrowser :: checkFileManagerEvent(void)
{
    FileManagerEvent *event = (FileManagerEvent *)observerQueue->waitForEvent(0);
    if (event) {
    	printf("Event %d on %s\n", event->eventType, event->pathName.c_str() );

    	// example: browser path = /SD/Hallo  Event = media removed /SD/

		Path *path = fm->get_new_path("handleEvent");
		path->cd(event->pathName.c_str()); // now we have a path object, with indexable elements :)

		TreeBrowserState *st = state_root;
		// TreeBrowserState *nst = state_root;

		bool match_dir = true;
		bool match_entry = false;

		for (int i=0; i<path->getDepth();i++) {
			if (st->deeper) {
				st = st -> deeper;
			} else {
				match_dir = false;
				break;
			}
			if (strcmp(st->node->getName(), path->getElement(i)) != 0) {
				match_dir = false;
			}
		}
		// match dir means: Our view is -or is a child of- the path mentioned in the event.
		bool match_exact_path = (match_dir) && (st == this->state);

		if (match_dir) {
			if (st->deeper) {
				printf("$%p:", st->deeper);
				printf("%p:", st->deeper->node);
				printf("%s -> %s\n", st->deeper->node->getName(), event->newName.c_str());
				if (strcmp(st->deeper->node->getName(), event->newName.c_str()) == 0) {
					match_entry = true;
				}
			}
		}

		printf("DIR %sMATCHED, ENTRY %sMATCHED, st = %s\n", match_dir?"":"NOT ", match_entry?"":"NOT ", st->node->getName());
		Browsable *b;

    	switch (event->eventType) {
    	case eNodeAdded:
    	case eRefreshDirectory:
    		if (match_exact_path) { // are we seeing the change?
    			state->refresh = true;
    		}
    		if (match_dir) {
    			st->needs_reload = true;
    		}
			break;

    	case eNodeRemoved: // node itself is gone
    		if (event->pathName == getPath()) { // are we only seeing the change?
    			printf("Refresh because event path and current path are equal.\n");
    			state->refresh = true;
    		} else {
    			if (match_entry) {
    				while ((state->previous) && (state != st)) {
    					state->level_up();
    				}
    			}
    			state->refresh = true;
    		}
    		if (match_dir) {
    			b = st->node->findChild(event->newName.c_str());
    			if (b) {
    				printf("Removing %s\n", b->getName());
    				st->node->children.remove(b);
    			}
    		}
    		break;

    	case eNodeMediaRemoved: // node loses all children
    		if (event->pathName == getPath()) {
    			state->refresh = true;
    		}
			if (match_entry) {
				while ((state->previous) && (state != st)) {
					state->level_up();
				}
			}
			state->refresh = true;
    		break;

    	case eNodeUpdated: // one element in the list got updated
    		if (event->pathName == getPath()) {
    			state->refresh = true;
    		}
    		break;
    	default:
    		break;
    	}
    }
}


void TreeBrowser :: tasklist(void)
{
	char *buffer = new char[8192];
    vTaskList(buffer);
    for(char *b = buffer; *b; b++) {
    	if (*b == 9)
    		*b = 32;
    }
    user_interface->run_editor(buffer);
    delete buffer;
}

int TreeBrowser :: handle_key(int c)
{           
    int ret = 0;
    
    switch(c) {
        case KEY_BREAK: // runstop
            ret = -1;
            break;
        case KEY_F8: // exit (F8)
            ret = -1;
            break;
        case KEY_DOWN: // down
        	reset_quick_seek();
            state->down(1);
            break;
        case KEY_UP: // up
        	reset_quick_seek();
            state->up(1);
            break;
        case KEY_F1: // F1 -> page up
        case KEY_PAGEUP:
        	reset_quick_seek();
            state->up(window->get_size_y()/2);
            break;
        case KEY_F3: // F3 -> help
        	reset_quick_seek();
        	state->refresh = true;
        	user_interface->run_editor(helptext);
            break;
		case KEY_F5: // F5: Menu
			task_menu();
			break;
        case KEY_F7: // F7 -> page down
        case KEY_PAGEDOWN:
        	reset_quick_seek();
            state->down(window->get_size_y()/2);
            break;
        case KEY_F2: // F2 -> config
            config();
            break;
        case KEY_F4: // F4 -> show threads
        	tasklist();
        	break;
/*
        case KEY_F6: // F6 -> show log
        	reset_quick_seek();
        	state->refresh = true;
        	user_interface->run_editor(textLog.getText());
        	break;
*/
        case KEY_BACK: // backspace
            if(quick_seek_length) {
                quick_seek_length--;
                perform_quick_seek();
            }
            break;
        case KEY_SPACE: // space = select
        	state->select();
        	break;
        case KEY_CTRL_A: // select all
        	state->select_all(true);
        	break;
        case KEY_CTRL_N: // select none
        	state->select_all(false);
        	break;
        case KEY_CTRL_C: // copy
        	copy_selection();
        	break;
        case KEY_CTRL_V: // paste
        	paste();
        	break;
        case KEY_RETURN: // CR = select
            reset_quick_seek();
            context(0);
            break;
        case KEY_RIGHT: // right
            reset_quick_seek();
			if (state->into2()) context(0);
            break;
        case KEY_LEFT: // left
        	state->level_up();
            break;
        default:
            if((c >= '!')&&(c < 0x80)) {
                if(quick_seek_length < (MAX_SEARCH_LEN_TB-2)) {
                    quick_seek_string[quick_seek_length++] = c;
                    if(!perform_quick_seek())
                        quick_seek_length--;
                }
            } else {
                printf("Unhandled key: %b\n", c);
            }
    }    
    return ret;
}

bool TreeBrowser :: perform_quick_seek(void)
{
    if(!state->under_cursor)
        return false;

    quick_seek_string[quick_seek_length] = '*';
    quick_seek_string[quick_seek_length+1] = 0;
    printf("Performing seek: '%s'\n", quick_seek_string);

    int num_el = state->children->get_elements();
    for(int i=0;i<num_el;i++) {
    	Browsable *t = (*state->children)[i];
		if(pattern_match(quick_seek_string, t->getName(), false)) {
			state->move_to_index(i);
			return true;
		}
    }
    return false;
}

void TreeBrowser :: reset_quick_seek(void)
{
    quick_seek_length = 0;
}

void TreeBrowser :: copy_selection(void)
{
	clipboard.reset();
	clipboard.setPath(path->get_path());
	for(int i=0;i<state->children->get_elements();i++) {
		Browsable *t = (*state->children)[i];
		if (t && t->getSelection()) {
			clipboard.addFile(t->getName());
		}
	}
	if (clipboard.getNumberOfFiles() == 0) {
		Browsable *t = state->under_cursor;
		clipboard.addFile(t->getName());
	}
	printf("Copied %d files in path %s\n", clipboard.getNumberOfFiles(), clipboard.getPath());
}

void TreeBrowser :: paste(void)
{
	printf("Going to paste %d files from path %s\n", clipboard.getNumberOfFiles(), clipboard.getPath());

	int items = clipboard.getNumberOfFiles();
	user_interface->show_progress("Copying...", items);
	for (int i=0;i<items;i++) {
		const char *fn = clipboard.getFileNameByIndex(i);
		FRESULT res = fm->fcopy(clipboard.getPath(), fn, this->getPath());  // from path, filename, dest path
		if (res != FR_OK) {
			printf("Error while copying: %d %s to %s\n", res, fn, this->getPath());
			int resp = user_interface->popup("Copy error occurred. Continue?", BUTTON_YES | BUTTON_NO);
			if (resp == BUTTON_NO)
				break;
		}
		user_interface->update_progress(0, 1);
	}
	user_interface->hide_progress();
	state->refresh = true;
}


const char *TreeBrowser :: getPath() {
	return path->get_path();
}
