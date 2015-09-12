#include <stdio.h>
#include <string.h> // C library
#include "mystring.h" // my class definition
extern "C" {
    #include "small_printf.h"
}

mstring :: mstring()
{
//    printf("empty mstring creation.\n");
    alloc = 0;
    temporary = 0;
    cp = NULL;
}

mstring :: mstring(const char *k)
{
//    printf("Create mstring from char*. Source = %s\n", k);
    alloc = 1+strlen(k);
    temporary = 0;
    cp = new char[alloc];
    strcpy(cp, k);
}

mstring :: mstring(mstring &ref)
{
//    printf("Creating mstring from reference. (source: %s)\n", ref.cp);
    alloc = ref.alloc;
    temporary = 0;
    cp = new char[alloc];
    strcpy(cp, ref.cp);
}

mstring :: ~mstring()
{
//    printf("Deleting mstring %s (this=%p)\n", cp, this);
    if(cp)
        delete[] cp;
}

const char *mstring :: c_str(void)
{
    if(cp)
        return cp;
    return "";
}

int mstring :: length(void)
{
    if(cp)
        return strlen(cp);
    return 0;
}

int mstring :: allocated_space(void)
{
    return alloc;
}
    
mstring& mstring :: operator=(const char *rhs)
{
//    printf("Operator = char*rhs=%s. This = %p.\n", rhs, this);
    int n = strlen(rhs);
    if((n+1) > alloc) {    
        delete[] cp;
        cp = new char[n+1];
        alloc = n+1;
    }
    strcpy(cp, rhs);
    return *this;
}

mstring& mstring :: operator=(mstring &rhs)
{
//    printf("Assignment operator. Left = %s(%p), Right = %s(%p)\n", c_str(), this, rhs.c_str(), &rhs);
    if(this != &rhs) {
        if((rhs.length()+1) > alloc) {
            delete[] cp;
            cp = new char[rhs.alloc];
            alloc = rhs.alloc;
        }
        strcpy(cp, rhs.cp);
        if(rhs.temporary)
            delete &rhs;
    }
    return *this;
}

mstring& mstring :: operator+=(const char rhs)
{
    int n = length() + 2;
    if(n > alloc) { // doesnt fit
        char *new_cp = new char[n];
        alloc = n;
        strcpy(new_cp, cp);
        new_cp[n-2] = rhs;
        new_cp[n-1] = 0;
        delete[] cp;
        cp = new_cp;
    } else { // fits
        cp[n-2] = rhs;
        cp[n-1] = 0;
    }
    return *this;
}

mstring& mstring :: operator+=(const char *rhs)
{
    int n = length() + strlen(rhs) + 1;
//    printf("New n = %d.\n", n);
    if(n > alloc) { // doesnt fit
        char *new_cp = new char[n];
        alloc = n;
        strcpy(new_cp, cp);
        strcat(new_cp, rhs);
        delete[] cp;
        cp = new_cp;
//        printf("%s\n", new_cp);
    } else { // fits
//        printf("Fits!");
        strcat(cp, rhs);
    }
    return *this;
}

mstring& mstring :: operator+=(mstring &rhs)
{
    int n = length() + rhs.length() + 1;
    if(n > alloc) { // doesnt fit
        char *new_cp = new char[n];
        alloc = n;
        strcpy(new_cp, cp);
        strcat(new_cp, rhs.cp);
        delete[] cp;
        cp = new_cp;
    } else { // fits
        strcat(cp, rhs.cp);
    }
    return *this;
}

bool mstring :: operator==(mstring &rhs)
{
    if(!cp && !rhs.cp)
        return true;
    if(!cp)
        return false;
    if(!rhs.cp)
        return false;
    return (strcmp(cp, rhs.cp) == 0);
}

bool mstring :: operator==(const char *rhs)
{
    if(!cp && !rhs)
        return true;
    if(!cp)
        return false;
    if(!rhs)
        return false;
    return (strcmp(cp, rhs) == 0);
}

mstring& operator+(mstring &left, mstring &right)
{
//    printf("+ operator. Left = (%s)%p, Right = (%s)%p\n", left.cp, &left, right.cp, &right);
    mstring *result = new mstring(left);
    result->temporary = 1;
    (*result)+=right;
    return *result;
}

mstring& operator+(mstring &left, const char *right)
{
//    printf("+ operator. Left = (%s)%p, Right = %s\n", left.cp, &left, right);
    mstring *result = new mstring(left);
    result->temporary = 1;
    (*result)+=right;
    return *result;
}

mstring& operator+(const char *left, mstring &right)
{
//    printf("+ operator. Left = %s, Right = (%s)%p\n", left, right.cp, &right);
    mstring *result = new mstring(left);
    result->temporary = 1;
    (*result)+=right;
    return *result;
}

int strcmp(mstring &a, mstring &b)
{
    if(!a.cp && !b.cp)
        return 0;
    if(!a.cp)
        return -1;
    if(!b.cp)
        return 1;
    return strcmp(a.cp, b.cp);
}

int strinscmp(mstring &a, mstring &b)
{
    if(!a.cp && !b.cp)
        return 0;
    if(!a.cp)
        return -1;
    if(!b.cp)
        return 1;
    return strcasecmp(a.cp, b.cp);
}

mstring& int_to_mstring(int i)
{
    char temp[16];
    sprintf(temp, "%d", i);
    mstring *result = new mstring(temp);
    result->temporary = 1;
    return *result;
}


#ifdef TESTSTR
#include <stdio.h>
    mstring s1;
    mstring s2("Hello");
    mstring s3(s2);

int main()
{

    printf("s1 = %p, s2 = %p, s3 = %p\n", &s1, &s2, &s3);

    printf("S1 = %s\n", s1.c_str());
    printf("S2 = %s\n", s2.c_str());
    printf("S3 = %s\n", s3.c_str());
    
    printf("s1 = \"Gideon\";\ns2 = \"kort\";\ns3 = \"veel langer\";\n");

    s1 = "Gideon";
    s2 = "kort";
    s3 = "veel langer";

    printf("S1 = %s (%d)\n", s1.c_str(), s1.allocated_space());
    printf("S2 = %s (%d)\n", s2.c_str(), s2.allocated_space());
    printf("S3 = %s (%d)\n", s3.c_str(), s3.allocated_space());
    
    printf("s1 = s3;\ns3 = s2;\ns2 += s1;\n");

    s1 = s3;
    s3 = s2;
    s2 += s1;

    printf("S1 = %s (%d)\n", s1.c_str(), s1.allocated_space());
    printf("S2 = %s (%d)\n", s2.c_str(), s2.allocated_space());
    printf("S3 = %s (%d)\n", s3.c_str(), s3.allocated_space());

    s1 = "A ";
    s2 = "B ";
    s3 = "C ";

    printf("S1 = %s (%d)\n", s1.c_str(), s1.allocated_space());
    printf("S2 = %s (%d)\n", s2.c_str(), s2.allocated_space());
    printf("S3 = %s (%d)\n", s3.c_str(), s3.allocated_space());

    printf("s1 += \" <= Hello\";\ns3 = s2 + \"Zweijtzer\";\n");
    s1 += " <= Hello";
    s3 = s2 + "Zweijtzer";
    s1 = s1 + s1;
    
    if(s3 == "B Zweijtzer")
        printf("Success!\n");
        
    printf("S1 = %s (%d)\n", s1.c_str(), s1.allocated_space());
    printf("S2 = %s (%d)\n", s2.c_str(), s2.allocated_space());
    printf("S3 = %s (%d)\n", s3.c_str(), s3.allocated_space());

}

#endif
