#ifndef MYSTRING_H
#define MYSTRING_H

#ifndef NULL
#define NULL (0)
#endif

class mstring
{
private:
    short alloc;
    char *cp;
public:
    mstring();
    mstring(const char *k);
    mstring(mstring &k);
    ~mstring();
    
    const char *c_str(void);
    const int length(void) const;
    const int allocated_space(void) const;
    void to_upper(void);
    void set(int index, char c);

    mstring& operator=(const char *rhs);
    mstring& operator=(const mstring &rhs);

    mstring& operator+=(const char rhs);
    mstring& operator+=(const char *rhs);
    mstring& operator+=(const mstring &rhs);
    bool operator==(const mstring &rhs);
    bool operator==(const char *rhs);

    mstring operator+(const mstring &);
    mstring operator+(const char *);
    friend int strcmp(mstring &a, mstring &b);
    friend int strinscmp(mstring &a, mstring &b);
    friend mstring& int_to_mstring(int i);
};

int strcmp(mstring &a, mstring &b);
int stricmp(mstring &a, mstring &b);
mstring& int_to_mstring(int i);

#endif
