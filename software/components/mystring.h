#ifndef MYSTRING_H
#define MYSTRING_H

#ifndef NULL
#define NULL (0)
#endif

class mstring
{
private:
    short temporary;
    short alloc;
    char *cp;
public:
    mstring();
    mstring(const char *k);
    mstring(mstring &k);
    ~mstring();
    
    const char *c_str(void);
    int length(void);
    int allocated_space(void);

    mstring& operator=(const char *rhs);
    mstring& operator=(mstring &rhs);

    mstring& operator+=(const char rhs);
    mstring& operator+=(const char *rhs);
    mstring& operator+=(mstring &rhs);
    bool operator==(mstring &rhs);
    bool operator==(const char *rhs);

    friend mstring& operator+(mstring &, mstring &);
    friend mstring& operator+(mstring &, const char *);
    friend mstring& operator+(const char *, mstring &);
    friend int strcmp(mstring &a, mstring &b);
    friend int strinscmp(mstring &a, mstring &b);
    friend mstring& int_to_mstring(int i);
};

mstring& operator+(mstring &left, mstring &right);
mstring& operator+(mstring &left, const char *rhs);
mstring& operator+(const char *left, mstring &rhs);
//mstring& operator+(char *left, char *right);
int strcmp(mstring &a, mstring &b);
int stricmp(mstring &a, mstring &b);
mstring& int_to_mstring(int i);

#endif
