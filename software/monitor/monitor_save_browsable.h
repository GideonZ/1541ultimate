#ifndef MONITOR_SAVE_BROWSABLE_H
#define MONITOR_SAVE_BROWSABLE_H

#include "browsable.h"
#include "browsable_root.h"
#include "filemanager.h"
#include "small_printf.h"

// Monitor save-mode browser entries. The synthetic <Create File>
// entry is injected by the monitor-owned root and directory subclasses, so
// generic browser code does not need to know about save semantics — it only
// needs the pickAsCurrentPath() hook.

class MonitorSaveSelectFolderEntry : public Browsable
{
public:
    MonitorSaveSelectFolderEntry() { selectable = true; }

    const char *getName() { return "<Create File>"; }

    void getDisplayString(char *buffer, int width)
    {
        getDisplayString(buffer, width, 0);
    }

    void getDisplayString(char *buffer, int width, int squeeze_option)
    {
        (void)squeeze_option;
        if (width <= 0) return;
        const char *label = getName();
        int len = (int)strlen(label);
        if (len > width - 1) len = width - 1;
        memcpy(buffer, label, len);
        buffer[len] = 0;
    }

    bool pickAsCurrentPath() { return true; }
};

static inline void monitor_save_prepend_synthetic(IndexedList<Browsable *> &children)
{
    children.append(new MonitorSaveSelectFolderEntry());
    for (int i = children.get_elements() - 1; i > 0; i--) {
        children.swap(i, i - 1);
    }
}

class MonitorSaveDirEntry : public BrowsableDirEntry
{
public:
    MonitorSaveDirEntry(Path *pp, Browsable *parent, FileInfo *info, bool sel)
        : BrowsableDirEntry(pp, parent, info, sel) {}

protected:
    Browsable *makeChildEntry(Path *parent_path, FileInfo *info, bool selectable)
    {
        return new MonitorSaveDirEntry(parent_path, this, info, selectable);
    }

public:
    IndexedList<Browsable *> *getSubItems(int &error)
    {
        IndexedList<Browsable *> *list = BrowsableDirEntry::getSubItems(error);
        // Inject the synthetic <Create File> on any successful listing — covers
        // ordinary directories AND mountable disk images (D64/T64/G64/D81/...)
        // since FileManager surfaces their contents through get_directory().
        // Plain non-mountable files have error<0 here, so they're skipped.
        if (error == 0 && list) {
            bool already = false;
            for (int i = 0; i < list->get_elements() && !already; i++) {
                Browsable *b = (*list)[i];
                if (b && b->pickAsCurrentPath()) already = true;
            }
            if (!already) {
                monitor_save_prepend_synthetic(*list);
            }
        }
        return list;
    }
};

// Root listing only injects the typed children — no synthetic save entry.
// The root holds mount points (USB, Flash, Temp, network), none of which is a
// writable destination on its own; offering "save here" there would mislead.
class MonitorSaveBrowsableRoot : public BrowsableRoot
{
protected:
    Browsable *makeChildEntry(Path *parent_path, FileInfo *info, bool selectable)
    {
        return new MonitorSaveDirEntry(parent_path, this, info, selectable);
    }
};

#endif // MONITOR_SAVE_BROWSABLE_H
