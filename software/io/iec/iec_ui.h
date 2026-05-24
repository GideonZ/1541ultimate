#include "tree_browser.h"
#include "iec_channel.h"

class BrowsableIecNewPartition : public Browsable
{
    Browsable *collection;
    static SubsysResultCode_e S_add(SubsysCommand *cmd);
public:
    BrowsableIecNewPartition(Browsable *par) : collection(par) { }
    virtual ~BrowsableIecNewPartition() { }

    const char *getName() { return "New IEC Partition"; }
    virtual void getDisplayString(char *buffer, int width) {
        snprintf(buffer, width-1, "<Add>");
    }
	virtual IndexedList<Browsable *> *getSubItems(int &error) { error = -1; return NULL; }
    virtual void fetch_context_items(IndexedList<Action *>&items) {
        items.append(new Action("Add Partition", BrowsableIecNewPartition :: S_add, 0, 0, this));
    }
    IecFileSystem *getIecFileSystem();
    TreeBrowser *getBrowser();
};

class BrowsableIecPartition : public BrowsableIecNewPartition
{
    int num;
    mstring name, path;
    static SubsysResultCode_e S_edit(SubsysCommand *cmd);
    static SubsysResultCode_e S_remove(SubsysCommand *cmd);
public:
    BrowsableIecPartition(Browsable *par, int num, const char *name, const char *path) : BrowsableIecNewPartition(par), num(num), name(name), path(path) { }
    virtual ~BrowsableIecPartition() { }

    const char *getName() { return name.c_str(); }
    void getDisplayString(char *buffer, int width) {
        snprintf(buffer, width-1, "%3d: %12s (%16s)", num, name.c_str(), path.c_str());
    }
    void fetch_context_items(IndexedList<Action *>&items) {
        items.append(new Action("Edit",   BrowsableIecPartition :: S_edit, 0, 0, this));
        items.append(new Action("Remove", BrowsableIecPartition :: S_remove, 0, 0, this));
    }
};

class BrowsableIecPartitions : public Browsable
{
    IecFileSystem *vfs;
    TreeBrowser *browser;
public:
    BrowsableIecPartitions(IecFileSystem *vfs) : vfs(vfs) { }
    virtual ~BrowsableIecPartitions() { }

    const char *getName() { return "BrowsableIecPartitions"; }
    void setBrowser(TreeBrowser *tb) { browser = tb; }
	IndexedList<Browsable *> *getSubItems(int &error) {
        error = 0;
        for(int i=1; i<MAX_PARTITIONS; i++) {
            IecPartition *p = vfs->GetPartition(i);
            if (p) {
                children.append(new BrowsableIecPartition(this, i, p->GetName(), p->GetRootPath()));
            }
        }
        children.append(new BrowsableIecNewPartition(this));
        return &children;
    }
    IecFileSystem *getIecFileSystem() { return vfs; }
    TreeBrowser *getBrowser() { return browser; }
};

void show_partitions(UserInterface *ui, IecFileSystem *vfs);
int form_new_partition(UserInterface *ui, JSON_Object *form_fields);
