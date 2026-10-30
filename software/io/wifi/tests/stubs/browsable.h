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
class UserInterface {
public:
    static void postMessage(const char *msg);
    int string_box(const char *msg, char *buffer, int maxlen);
};
class Action;
class SubsysCommand { public: intptr_t functionID; UserInterface *user_interface; };
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
