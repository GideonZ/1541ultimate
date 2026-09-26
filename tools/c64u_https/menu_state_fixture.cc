// Minimal external dependencies; production state cleanup is inserted below.
#include <cassert>
#include <cstdio>
#include <set>
template<class T> struct IndexedList {};
struct Browsable {
    static int instances;
    int cleanups = 0;
    Browsable() { ++instances; }
    ~Browsable() { --instances; }
    void killChildren() { ++cleanups; }
    const char *getName() { return "test root"; }
};
int Browsable::instances = 0;
IndexedList<Browsable *> emptyList;
struct UserInterface {};
struct UIObject {
    explicit UIObject(UserInterface *) {}
    void setCleanup() {}
};
struct Path {};
struct ObserverQueue { explicit ObserverQueue(const char *) {} };
struct FileManager {
    int paths = 0, observers = 0;
    static FileManager *getFileManager() { static FileManager fm; return &fm; }
    Path *get_new_path(const char *) { ++paths; return new Path; }
    void release_path(Path *p) { --paths; delete p; }
    void registerObserver(ObserverQueue *) { ++observers; }
    void deregisterObserver(ObserverQueue *) { --observers; }
};
class TreeBrowser;
struct TreeBrowserState;
static std::set<TreeBrowserState *> live;
struct TreeBrowserState {
    TreeBrowser *browser;
    int level, first_item_on_screen, selected_line, cursor_pos, initial_index;
    Browsable *node, *under_cursor;
    bool refresh, needs_reload;
    TreeBrowserState *previous, *deeper;
    IndexedList<Browsable *> *children;
    TreeBrowserState(Browsable *, TreeBrowser *, int);
    virtual ~TreeBrowserState();
    void cleanup();
};
struct ConfigBrowserState : TreeBrowserState {
    ConfigBrowserState(Browsable *, TreeBrowser *, int);
    ~ConfigBrowserState();
};
struct TreeBrowser : UIObject {
    bool allow_exit, has_path;
    UserInterface *user_interface;
    void *screen, *window, *keyb, *contextMenu;
    int quick_seek_length;
    char quick_seek_string[32];
    Browsable *root;
    TreeBrowserState *state, *state_root;
    FileManager *fm;
    Path *path;
    ObserverQueue *observerQueue;
    TreeBrowser(UserInterface *, Browsable *);
    virtual ~TreeBrowser();
    void replace_root_state(TreeBrowserState *);
};
struct ConfigBrowser : TreeBrowser {
    int start_level;
    ConfigBrowser(UserInterface *, Browsable *, int);
    ~ConfigBrowser();
};

// INSERT_PRODUCTION_LIFETIMES

int main()
{
    UserInterface ui;
    Browsable borrowed_root, nested_page;
    FileManager *fm = FileManager::getFileManager();
    // The ordinary browser keeps its own state and borrows its root.
    {
        TreeBrowser browser(&ui, &borrowed_root);
        assert(browser.state == browser.state_root);
        assert(live.size() == 1);
    }
    assert(live.empty());
    assert(borrowed_root.cleanups == 1);
    for (int cycle = 1; cycle <= 5; ++cycle) {
        const int before = borrowed_root.cleanups;
        TreeBrowser *browser = new ConfigBrowser(&ui, &borrowed_root, cycle % 2);
        assert((browser->state_root == browser->state) == !EXPECTED_ORPHANS);
        assert(dynamic_cast<ConfigBrowserState *>(browser->state));
        assert(browser->state->level == cycle % 2);
        // Exercise destruction from both the root and a nested configuration
        // state: the production destructor owns the previous-state chain.
        if (cycle % 2) {
            auto *child = new ConfigBrowserState(&nested_page, browser, 2);
            child->previous = browser->state;
            browser->state->deeper = child;
            browser->state = child;
        }
        delete browser; // virtual dispatch through the base class
        assert(live.size() == (EXPECTED_ORPHANS ? size_t(cycle) : 0));
        assert(borrowed_root.cleanups == before + (EXPECTED_ORPHANS ? 1 : 2));
        assert(Browsable::instances == 2); // neither borrowed node was deleted
        assert(fm->paths == 0 && fm->observers == 0);
        std::printf("CYCLE %d RETAINED_STATES %zu\n", cycle, live.size());
    }
    assert(nested_page.cleanups == 3);
    assert(live.size() == EXPECTED_ORPHANS);
    // Only the negative baseline test intentionally retains objects. Dispose
    // after measuring, so sanitizers still detect accidental fixture leaks.
    while (!live.empty()) delete *live.begin();
    // Heap-allocated roots are also borrowed: the external owner deletes them.
    Browsable *external_root = new Browsable;
    delete new ConfigBrowser(&ui, external_root, 1);
    assert(Browsable::instances == 3);
    while (!live.empty()) delete *live.begin();
    delete external_root;
    assert(Browsable::instances == 2);
    assert(fm->paths == 0 && fm->observers == 0);
    std::puts("Menu state ownership PASS");
}
