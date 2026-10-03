// Config stores own their items independently of the disposable menu wrappers.
enum { CFG_TYPE_SEP = 1, CFG_TYPE_INFO = 2 };
struct Definition { int type = 0; };
struct ConfigItem { Definition definition_value; Definition *definition = &definition_value; };
struct ConfigStore {
    IndexedList<ConfigItem *> items;
    ConfigStore() : items(4, NULL) { for (int i = 0; i < 3; ++i) items.append(new ConfigItem); }
    ~ConfigStore() { for (int i = 0; i < items.get_elements(); ++i) delete items[i]; }
    IndexedList<ConfigItem *> *getItems() { return &items; }
    void at_open_config() {}
    void at_close_config() {}
    void effectuate() {}
    bool isHidden() { return false; }
    const char *get_alt_store_name() { return "fixture store"; }
};
static ConfigStore store;
struct ConfigGroup {
    IndexedList<ConfigStore *> stores;
    ConfigGroup() : stores(1, NULL) { stores.append(&store); }
    IndexedList<ConfigItem *> *getConfigItems() { return store.getItems(); }
    IndexedList<ConfigStore *> *getStores() { return &stores; }
    const char *getName() { return "fixture group"; }
};
static ConfigGroup group;
struct ConfigManager {
    static ConfigManager *getConfigManager() { static ConfigManager manager; return &manager; }
    ConfigStore *find_store(const char *) { return &store; }
    IndexedList<ConfigStore *> *getStores() { return group.getStores(); }
};
struct ConfigGroupCollection {
    static ConfigGroup *getGroup(const char *, int) { return &group; }
    static IndexedList<ConfigGroup *> *getGroups() {
        static IndexedList<ConfigGroup *> groups(1, NULL);
        if (!groups.get_elements()) groups.append(&group);
        return &groups;
    }
};
enum { BR_EVENT_OUT = 1, BR_EVENT_CLOSE, BR_EVENT_OPEN };

// INSERT_PAGE_CLASSES
// INSERT_LIFETIMES
// INSERT_OWNED_BROWSER

void ConfigBrowser::init()
{
    int error = 0;
    state->children = root->getSubItems(error);
}
using SubsysResultCode_e = int;
static const int SSRET_OK = 0;
static const char *config_menu_names[] = { "fixture page" };
struct CommodoreMenu {
    UserInterface *user_interface;
    static SubsysResultCode_e S_cfg_page(Action *, void *);
    static SubsysResultCode_e S_cfg_group(Action *, void *);
    static SubsysResultCode_e S_cfg_audio(Action *, void *);
    static SubsysResultCode_e S_advanced(Action *, void *);
};
// INSERT_FACTORIES

static void close_menu(UserInterface &ui)
{
    assert(ui.active);
    delete ui.active;
    ui.active = NULL;
    assert(live.empty());
    FileManager *fm = FileManager::getFileManager();
    assert(fm->paths == 0 && fm->observers == 0);
}

int main()
{
    UserInterface ui;
    CommodoreMenu menu = { &ui };
    Action action;
    const int initial_nodes = *Browsable::getCount();
    for (auto factory : {CommodoreMenu::S_cfg_page, CommodoreMenu::S_cfg_group}) {
        std::vector<Browsable *> orphan_roots;
        for (int cycle = 1; cycle <= 5; ++cycle) {
            assert(factory(&action, &menu) == SSRET_OK);
            Browsable *root = ui.active->root;
            close_menu(ui);
            if (EXPECT_PAGE_LEAK) orphan_roots.push_back(root);
            assert(*Browsable::getCount() == initial_nodes + (EXPECT_PAGE_LEAK ? 4 * cycle : 0));
            std::printf("PAGE CYCLE %d RETAINED_NODES %d\n", cycle,
                        *Browsable::getCount() - initial_nodes);
        }
        // Preserve and measure the negative case, then clean up deliberately
        // orphaned roots so unrelated sanitizer findings remain meaningful.
        for (auto root : orphan_roots) delete root;
        assert(*Browsable::getCount() == initial_nodes);
    }
    if (!EXPECT_PAGE_LEAK) {
        // Advanced root owns store/group wrappers and their lazily created items.
        assert(CommodoreMenu::S_advanced(&action, &menu) == SSRET_OK);
        int error = 0;
        auto *pages = ui.active->root->getSubItems(error);
        for (int i = 0; i < pages->get_elements(); ++i) (*pages)[i]->getSubItems(error);
        // Close with a nested state still active: page deletion must come last.
        auto *nested = new ConfigBrowserState((*pages)[0], ui.active, 1);
        nested->previous = ui.active->state;
        ui.active->state = nested;
        close_menu(ui);
        assert(*Browsable::getCount() == initial_nodes);
    }
    // Audio's static root remains borrowed, with its existing cached children.
    Browsable *audio_root = NULL;
    int cached_count = 0;
    for (int cycle = 0; cycle < 5; ++cycle) {
        assert(CommodoreMenu::S_cfg_audio(&action, &menu) == SSRET_OK);
        if (!cycle) audio_root = ui.active->root;
        assert(ui.active->root == audio_root);
        close_menu(ui);
        if (!cycle) cached_count = *Browsable::getCount();
        assert(*Browsable::getCount() == cached_count);
    }
    assert(cached_count > initial_nodes);
    // The unchanged static predefined-root destructor intentionally retains
    // cached children for firmware lifetime. Dispose fixture cache on test exit.
    int error = 0;
    auto *cached = audio_root->getSubItems(error);
    for (int i = 0; i < cached->get_elements(); ++i) delete (*cached)[i];
    cached->clear_list();
    assert(*Browsable::getCount() == initial_nodes + 1); // static root survives
    if (!EXPECT_PAGE_LEAK) {
        // The ordinary advanced settings entry also borrows a static root.
        Browsable *settings_root = NULL;
        for (int cycle = 0; cycle < 5; ++cycle) {
            ConfigBrowser::start(&ui);
            if (!cycle) settings_root = ui.active->root;
            assert(ui.active->root == settings_root);
            close_menu(ui);
            if (!cycle) cached_count = *Browsable::getCount();
            assert(*Browsable::getCount() == cached_count);
        }
    }
    std::puts("Retail page ownership checks PASS");
}
