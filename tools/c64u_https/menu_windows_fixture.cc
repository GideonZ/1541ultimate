#include <cassert>
#include <cstdio>
#include <set>
#include <vector>

struct Screen {
    int color = 3, reverse = 0;
    int get_size_x() { return 40; }
    int get_size_y() { return 25; }
    int get_color() { return color; }
    void set_color(int value) { color = value; }
    void set_background(int) {}
    void reverse_mode(int value) { reverse = value; }
};
struct Keyboard {};
struct Window;
static std::set<Window *> live;
struct Window {
    Screen *parent;
    int old_color, window_x, window_y, offset_x, offset_y;
    int cursor_x, cursor_y, border_h, border_v;
    static int border_resets;
    Window(Screen *, int, int, int, int);
    virtual ~Window();
    void draw_border() {}
    void reset_border() { ++border_resets; }
    int get_size_y() { return window_y; }
    void move_cursor(int, int) {}
    void set_color(int v) { parent->set_color(v); }
    void reverse_mode(int v) { parent->reverse_mode(v); }
    void set_background(int v) { parent->set_background(v); }
    void output_line(const char *, int = 0) {}
};
int Window::border_resets = 0;
struct Action {
    static int destroyed;
    bool persistent;
    explicit Action(bool p = false) : persistent(p) {}
    ~Action() { ++destroyed; }
    bool isPersistent() { return persistent; }
    bool isEnabled() { return true; }
    const char *getName() { return "Example"; }
};
int Action::destroyed = 0;
struct Actions {
    std::vector<Action *> entries;
    int get_elements() { return entries.size(); }
    Action *operator[](int i) { return i < get_elements() ? entries[i] : nullptr; }
};
struct Host { void set_colors(int, int) {} };
struct UIObject {
    virtual ~UIObject() {}
    virtual void init() = 0;
    virtual void deinit() = 0;
    virtual void redraw() = 0;
};
struct UserInterface {
    Screen screen;
    Keyboard keyboard;
    Host host_object;
    Host *host = &host_object;
    int color_bg = 0, color_border = 0, color_sel = 1, reverse_sel = 0;
    int color_sel_bg = 0, color_fg = 1, focus = 0;
    bool doBreak = false;
    UIObject *ui_objects[2] = {};
    Screen *get_screen() { return &screen; }
    Keyboard *get_keyboard() { return &keyboard; }
    void set_screen_title() {}
    void appear();
    void release_host();
};
struct ContextMenu : UIObject {
    UserInterface *user_interface;
    Screen *screen = nullptr;
    Keyboard *keyb = nullptr;
    Window *window = nullptr;
    Actions actions;
    int item_index = 0, first = 0, indent = 0;
    explicit ContextMenu(UserInterface *ui) : user_interface(ui) {}
    ~ContextMenu();
    UserInterface *get_ui() { return user_interface; }
    void init() override {}
    void deinit() override;
    void draw();
    void redraw() override { draw(); }
};
struct CommodoreMenu : ContextMenu {
    explicit CommodoreMenu(UserInterface *ui) : ContextMenu(ui) {}
    void init() override;
};

// INSERT_LIFECYCLES

int main()
{
    UserInterface ui;
    // Persistent root survives hiding; its window must not survive release.
    {
        CommodoreMenu root(&ui);
        ui.ui_objects[0] = &root;
        for (int i = 1; i <= 10; ++i) {
            ui.appear();
            assert(live.size() == (EXPECT_WINDOW_LEAK ? size_t(i) : 1));
            ui.release_host();
            assert(live.size() == (EXPECT_WINDOW_LEAK ? size_t(i) : 0));
            if (!EXPECT_WINDOW_LEAK) {
                assert(!root.window);
                root.draw(); // safe after deinit and before the next init
                root.deinit(); // idempotent
                assert(ui.screen.color == 3 && ui.screen.reverse == 0);
            }
        }
        assert(Window::border_resets == 10);
    }
    assert(live.size() == (EXPECT_WINDOW_LEAK ? 10 : 0));
    std::printf("TEN ROOT CYCLES RETAINED_WINDOWS %zu\n", live.size());
    // Dispose measured negative-case orphans so sanitizers catch other errors.
    while (!live.empty()) delete *live.begin();
    Action persistent(true);
    {
        ContextMenu transient(&ui);
        transient.actions.entries = {new Action, &persistent};
        transient.window = new Window(&ui.screen, 0, 0, 10, 3);
        // Destruction without an earlier deinit must also release its window.
    }
    assert(Action::destroyed == 1); // borrowed persistent action remains alive
    assert(live.size() == (EXPECT_WINDOW_LEAK ? 1 : 0));
    while (!live.empty()) delete *live.begin();
    {
        ContextMenu empty(&ui);
        empty.deinit();
        if (!EXPECT_WINDOW_LEAK) empty.draw();
    }
    if (!EXPECT_WINDOW_LEAK) {
        CommodoreMenu root(&ui), child(&ui);
        ui.ui_objects[0] = &root;
        ui.ui_objects[1] = &child;
        ui.focus = 1;
        ui.appear();
        assert(live.size() == 2);
        ui.release_host(); // child then parent; restore original screen color
        assert(live.empty() && ui.screen.color == 3);
    }
    std::puts("Menu window lifetime comparison PASS");
}
