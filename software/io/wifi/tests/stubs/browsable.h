// Stand-in for browsable.h and what it brings: the pieces wifi.h's BrowsableWifiAP and the
// AP list declare against. IndexedList keeps what is appended, so that a test can read it.
#ifndef WIFI_TESTS_BROWSABLE_H
#define WIFI_TESTS_BROWSABLE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
template <class T> class IndexedList {
    std::vector<T> items;
public:
    IndexedList(int initial, T def) {}
    void append(T t) { items.push_back(t); }
    int get_elements(void) const { return (int)items.size(); }
    T operator[](int i) const { return items[i]; }
};
#define BUTTON_OK     0x01
#define BUTTON_YES    0x02
#define BUTTON_NO     0x04
class TreeBrowser;
class UserInterface {
public:
    static void postMessage(const char *msg);
    int string_box(const char *msg, char *buffer, int maxlen);
    int popup(const char *msg, uint8_t flags);
    void activate_uiobject(TreeBrowser *tb);
};
class Action;
class SubsysCommand {
public:
    intptr_t functionID;
    UserInterface *user_interface;
    SubsysCommand() : functionID(0), user_interface(NULL) {}
    SubsysCommand(UserInterface *u, int, int, int, const char *, const char *) : functionID(0), user_interface(u) {}
};
typedef enum { SSRET_OK = 0, SSRET_NO_USER_INTERFACE } SubsysResultCode_e;
class Browsable {
public:
    IndexedList<Browsable *> children;
    Browsable() : children(0, NULL) {}
    virtual ~Browsable() {}
    virtual void getDisplayString(char *buffer, int width, UserInterface *ui) {}
    virtual void fetch_context_items(IndexedList<Action *> &items) {}
    virtual IndexedList<Browsable *> *getSubItems(int &error) { return &children; }
};
class BrowsableStatic : public Browsable {
public:
    const char *text;
    BrowsableStatic(const char *t) : text(t) {}
};
#endif
