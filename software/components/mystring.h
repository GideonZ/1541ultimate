#ifndef MYSTRING_H
#define MYSTRING_H

#ifndef NULL
#define NULL (0)
#endif

class string
{
private:
    short temporary;
    short alloc;
    char *cp;
public:
    string();
    string(char *k);
    string(string &k);
    ~string();
    
    char *c_str(void);
    int length(void);
    int allocated_space(void);

    string& operator=(char *rhs);
    string& operator=(string &rhs);

    string& operator+=(char *rhs);
    string& operator+=(string &rhs);
    bool operator==(string &rhs);
    bool operator==(char *rhs);

    friend string& operator+(string &, string &);
    friend string& operator+(string &, char *);
    friend int strcmp(string &a, string &b);
    friend int stricmp(string &a, string &b);
    friend string& int_to_string(int i);
};

string& operator+(string &left, string &right);
string& operator+(string &left, char *rhs);
int strcmp(string &a, string &b);
int stricmp(string &a, string &b);
string& int_to_string(int i);

#endif
