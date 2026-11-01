#ifndef TEST_INSTALL_USERINTERFACE_H
#define TEST_INSTALL_USERINTERFACE_H
#include <string>
#include <vector>
#include <cstring>
enum { BUTTON_OK=1, BUTTON_YES=2, BUTTON_NO=4 };
class UserInterface {
public:
    std::string input="123456789abcdef0";
    int input_result=1, confirmation=BUTTON_YES;
    unsigned prompts=0;
    std::vector<std::string> messages;
    int string_box(const char *,char *out,int limit) {
        ++prompts;
        unsigned n=input.size()>unsigned(limit) ? unsigned(limit) : input.size();
        memcpy(out,input.data(),n); out[n]=0;
        return input_result;
    }
    int popup(const char *text,int flags) {
        messages.push_back(text);
        return flags==BUTTON_OK ? BUTTON_OK : confirmation;
    }
};
#endif
