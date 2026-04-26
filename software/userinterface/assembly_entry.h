#ifndef ASSEMBLY_ENTRY
#define ASSEMBLY_ENTRY

#include "browsable_root.h"
#include "assembly.h"
#include "json.h"
#include <ctype.h>

class BrowsableDirEntryAssembly : public Browsable
{
    mstring id;
    int category;
    int index;
    int size;
    mstring filename;
    mstring indexstr;
    BrowsableDirEntry *wrappedEntry;
    FileType *filetype;
public:
    BrowsableDirEntryAssembly(BrowsableQueryResult *parent, JSON_Object *obj, const char *id, int cat)
    {
        this->id = id;
        this->category = cat;
        filetype = NULL;
        index = 0;
        indexstr = "0";
        size = 65536;
        JSON *j = obj->get("path");
        if (j && j->type() == eString) {
            filename = ((JSON_String *)j)->get_string();
        }
        j = obj->get("id");
        if (j && j->type() == eInteger) {
            index = ((JSON_Integer *)j)->get_value();
            indexstr = "";
            indexstr += index;
        }
        j = obj->get("size");
        if (j && j->type() == eInteger) {
            size = ((JSON_Integer *)j)->get_value();
        }
        FileInfo *info = new FileInfo(filename.c_str());
        info->size = size;
        get_extension(filename.c_str(), info->extension);
        for(int i=0;i<3;i++) {
            if (!info->extension[i])
                break;
            info->extension[i] = toupper(info->extension[i]);
        }
        wrappedEntry = new BrowsableDirEntry(parent->getPath(), parent, info, true);
    }

    ~BrowsableDirEntryAssembly() { 
        delete wrappedEntry;
    }

    void setSelection(bool s)    { wrappedEntry->setSelection(s); }
    void allowSelectable(bool b) { wrappedEntry->allowSelectable(b); }
    bool getSelection()          { return wrappedEntry->getSelection(); }
    bool isSelectable()          { return wrappedEntry->isSelectable(); }

    const char *getName() {
        return filename.c_str();
    }

    void getDisplayString(char *buffer, int width) {
        wrappedEntry->getDisplayString(buffer, width,0);
    }

	void fetch_context_items(IndexedList<Action *>&items) {
		if (!filetype)
			filetype = FileType :: getFileTypeFactory()->create(wrappedEntry);
		if (filetype) {
			filetype->fetch_context_items(items);
		}
		UserFileInteraction :: getUserFileInteractionObject() -> fetch_context_items(wrappedEntry, items);
	}

    IndexedList<Browsable *> *getSubItems(int &error) {
        return wrappedEntry->getSubItems(error);
    }
};
#endif
