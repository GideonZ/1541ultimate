/*
    $Id: file.c 1510 2017-04-30 21:51:04Z soci $

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/
#include "file.h"
#include <string.h>
#include "wchar.h"
#include <errno.h>
#ifdef _WIN32
#include <locale.h>
#include <windows.h>
#endif
#include "64tass.h"
#include "unicode.h"
#include "error.h"
#include "strobj.h"
#include "arguments.h"

#define REPLACEMENT_CHARACTER 0xfffd

struct include_list_s {
    struct include_list_s *next;
#if __STDC_VERSION__ >= 199901L
    char path[];
#elif __GNUC__ >= 3
    char path[];
#else
    char path[1];
#endif
};

static struct include_list_s include_list;
struct include_list_s *include_list_last = &include_list;

static struct avltree file_tree;

void include_list_add(const char *path)
{
    size_t i, j, len;
    j = i = strlen(path);
    if (i == 0) return;
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    if (path[i - 1] != '/' && path[i-1] != '\\') j++;
#else
    if (path[i - 1] != '/') j++;
#endif
    len = j + 1 + sizeof(struct include_list_s);
    if (len < sizeof(struct include_list_s)) err_msg_out_of_memory();
    include_list_last->next = (struct include_list_s *)mallocx(len);
    include_list_last = include_list_last->next;
    include_list_last->next = NULL;
    memcpy(include_list_last->path, path, i + 1);
    if (i != j) memcpy(include_list_last->path + i, "/", 2);
}

#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
static inline bool is_driveletter(const char *name) {
    uint8_t c = name[0] | 0x20;
    return c >= 'a' && c <= 'z' && name[1] == ':';
}
#endif

char *get_path(const str_t *v, const char *base) {
    char *path;
    size_t i, len;
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    size_t j;

    i = strlen(base);
    j = is_driveletter(base) ? 2 : 0;
    while (i > j) {
        if (base[i - 1] == '/' || base[i - 1] == '\\') break;
        i--;
    }
#else
    const char *c;
    c = strrchr(base, '/');
    i = (c != NULL) ? (size_t)(c - base) + 1 : 0;
#endif

    if (v == NULL) {
        len = i + 1;
        if (len < 1) err_msg_out_of_memory(); /* overflow */
        path = (char *)mallocx(len);
        memcpy(path, base, i);
        path[i] = 0;
        return path;
    }

#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    if (v->len != 0 && (v->data[0] == '/' || v->data[0] == '\\')) i = j;
    else if (v->len > 1 && is_driveletter((const char *)v->data)) i = 0;
#else
    if (v->len != 0 && v->data[0] == '/') i = 0;
#endif
    len = i + v->len;
    if (len < i) err_msg_out_of_memory(); /* overflow */
    len += 1;
    if (len < 1) err_msg_out_of_memory(); /* overflow */
    path = (char *)mallocx(len);
    memcpy(path, base, i);
    memcpy(path + i, v->data, v->len);
    path[i + v->len] = 0;
    return path;
}

#ifdef _WIN32
static MUST_CHECK wchar_t *convert_name(const char *name, size_t max) {
    wchar_t *wname;
    uchar_t ch;
    size_t i = 0, j = 0, len = ((max != SIZE_MAX) ? max : strlen(name)) + 2;
    if (len > SIZE_MAX / sizeof *wname) return NULL;
    wname = (wchar_t *)malloc(len * sizeof *wname);
    if (wname == NULL) return NULL;
    while (name[i] != 0 && i < max) {
        ch = name[i];
        if ((ch & 0x80) != 0) {
            i += utf8in((const uint8_t *)name + i, &ch); 
            if (ch == 0) ch = REPLACEMENT_CHARACTER;
        } else i++;
        if (j + 3 > len) {
            wchar_t *d;
            len += 64;
            if (len > SIZE_MAX / sizeof *wname) goto failed;
            d = (wchar_t *)realloc(wname, len * sizeof *wname);
            if (d == NULL) goto failed;
            wname = d;
        }
        if (ch < 0x10000) {
        } else if (ch < 0x110000) {
            wname[j++] = (ch >> 10) + 0xd7c0;
            ch = (ch & 0x3ff) | 0xdc00;
        } else ch = REPLACEMENT_CHARACTER;
        wname[j++] = ch;
    }
    wname[j] = 0;
    return wname;
failed:
    free(wname);
    return NULL;
}
#endif

static bool portability(const str_t *name, linepos_t epoint) {
#ifdef _WIN32
    DWORD ret;
    wchar_t *wname = convert_name((const char *)name->data, name->len);
    size_t len;
    wchar_t *wname2;
    bool different;
    if (wname == NULL) return false;
    len = wcslen(wname) + 1;
    wname2 = (wchar_t *)malloc(len * sizeof *wname2);
    if (wname2 == NULL) {
        free(wname);
        return false;
    }
    ret = GetLongPathNameW(wname, wname2, len);
    different = ret != 0 && memcmp(wname, wname2, ret * sizeof *wname2) != 0;
    free(wname2);
    free(wname);
    if (different) {
        err_msg2(ERROR___INSENSITIVE, name, epoint);
        return false;
    }
#endif
#if defined _WIN32 || defined __WIN32__ || defined __EMX__ || defined __MSDOS__ || defined __DOS__
    if (memchr(name->data, '\\', name->len) != NULL) {
        err_msg2(ERROR_____BACKSLASH, name, epoint);
        return false;
    }
    if ((name->len > 0 && name->data[0] == '/') || (name->len > 1 && is_driveletter((const char *)name->data))) {
        err_msg2(ERROR_ABSOLUTE_PATH, name, epoint);
        return false;
    }
#else
    size_t i;
    const uint8_t *c = name->data;
    for (i = 0; i < name->len; i++) {
        if (strchr("\\:*?\"<>|", c[i]) != NULL) {
            err_msg2(ERROR__RESERVED_CHR, name, epoint);
            return false;
        }
    }
    if (name->len > 0 && name->data[0] == '/') {
        err_msg2(ERROR_ABSOLUTE_PATH, name, epoint);
        return false;
    }
#endif
    return true;
}

FILE *file_open(const char *name, const char *mode) {
    FILE *f;
#ifdef _WIN32
    wchar_t *wname, *c2, wmode[3];
    const uint8_t *c;
    wname = convert_name(name, SIZE_MAX);
    if (wname == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    c2 = wmode; c = (uint8_t *)mode;
    while ((*c2++=(wchar_t)*c++) != 0);
    f = _wfopen(wname, wmode);
    free(wname);
#else
    size_t len = 0, max = strlen(name) + 1;
    char *newname = (char *)malloc(max);
    const uint8_t *c = (const uint8_t *)name;
    uchar_t ch;
    mbstate_t ps;
    errno = ENOMEM;
    f = NULL;
    if (newname == NULL || max < 1) goto failed;
    memset(&ps, 0, sizeof ps);
    do {
        char temp[64];
        ssize_t l;
        ch = *c;
        if ((ch & 0x80) != 0) {
            c += utf8in(c, &ch); 
            if (ch == 0) {errno = ENOENT; goto failed;}
        } else c++;
        l = (ssize_t)wcrtomb(temp, (wchar_t)ch, &ps);
        if (l <= 0) goto failed;
        len += (size_t)l;
        if (len < (size_t)l) goto failed;
        if (len > max) {
            char *d;
            max = len + 64;
            if (max < 64) goto failed;
            d = (char *)realloc(newname, max);
            if (d == NULL) goto failed;
            newname = d;
        }
        memcpy(newname + len - l, temp, (size_t)l);
    } while (ch != 0);
    f = fopen(newname, mode);
failed:
    free(newname);
#endif
    return f;
}

static int star_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct star_s *a = cavltree_container_of(aa, struct star_s, node);
    const struct star_s *b = cavltree_container_of(bb, struct star_s, node);

    if (a->line != b->line) return a->line > b->line ? 1 : -1;
    return 0;
}

static int file_compare(const struct avltree_node *aa, const struct avltree_node *bb)
{
    const struct file_s *a = cavltree_container_of(aa, struct file_s, node);
    const struct file_s *b = cavltree_container_of(bb, struct file_s, node);
    int c = strcmp(a->name, b->name);
    if (c != 0) return c;
    c = strcmp(a->base, b->base);
    if (c != 0) return c;
    return a->type - b->type;
}

static void star_free(struct avltree_node *aa)
{
    struct star_s *a = avltree_container_of(aa, struct star_s, node);

    avltree_destroy(&a->tree, star_free);
}

static void file_free(struct avltree_node *aa)
{
    struct file_s *a = avltree_container_of(aa, struct file_s, node);

    avltree_destroy(&a->star, star_free);
    free(a->data);
    free(a->line);
    free((char *)a->name);
    free((char *)a->realname);
    free((char *)a->base);
    free(a);
}

static bool extendfile(struct file_s *tmp) {
    uint8_t *d;
    size_t len2 = tmp->len + 4096;
    if (len2 < 4096) return true; /* overflow */
    d = (uint8_t *)realloc(tmp->data, len2);
    if (d == NULL) return true;
    tmp->data = d;
    tmp->len = len2;
    return false;
}

static uint8_t *flushubuff(struct ubuff_s *ubuff, uint8_t *p, struct file_s *tmp) {
    size_t i;
    if (ubuff->data == NULL) {
        ubuff->data = (uchar_t *)malloc(16 * sizeof *ubuff->data);
        if (ubuff->data == NULL) return NULL;
        ubuff->len = 16;
        return p;
    }
    for (i = 0; i < ubuff->p; i++) {
        size_t o = (size_t)(p - tmp->data);
        uchar_t ch;
        if (o + 6*6 + 1 > tmp->len) {
            if (extendfile(tmp)) return NULL;
            p = tmp->data + o;
        }
        ch = ubuff->data[i];
        if (ch != 0 && ch < 0x80) *p++ = (uint8_t)ch; else p = utf8out(ch, p);
    }
    return p;
}

static bool extendbuff(struct ubuff_s *ubuff) {
    uchar_t *d;
    size_t len2 = ubuff->len + 16;
    if (/*len2 < 16 ||*/ len2 > SIZE_MAX / sizeof *ubuff->data) return true; /* overflow */
    d = (uchar_t *)realloc(ubuff->data, len2 * sizeof *ubuff->data);
    if (d == NULL) return true;
    ubuff->data = d;
    ubuff->len = len2;
    return false;
}

static uchar_t fromiso2(uchar_t c) {
    static mbstate_t ps;
    wchar_t w;
    int olderrno;
    ssize_t l;
    uint8_t c2 = (uint8_t)(c | 0x80);

    memset(&ps, 0, sizeof ps);
    olderrno = errno;
    l = (ssize_t)mbrtowc(&w, (char *)&c2, 1,  &ps);
    errno = olderrno;
    if (l < 0) return c2;
    return (uchar_t)w;
}

static inline uchar_t fromiso(uchar_t c) {
    static uchar_t conv[128];
    c &= 0x7f;
    if (conv[c] == 0) conv[c] = fromiso2(c);
    return conv[c];
}

static struct file_s *command_line = NULL;
static struct file_s *lastfi = NULL;
static uint16_t curfnum = 1;
struct file_s *openfile(const char* name, const char *base, int ftype, const str_t *val, linepos_t epoint) {
    char *base2;
    struct avltree_node *b;
    struct file_s *tmp;
    char *s;
    if (lastfi == NULL) {
        lastfi = (struct file_s *)mallocx(sizeof *lastfi);
    }
    base2 = get_path(NULL, base);
    lastfi->base = base2;
    lastfi->type = ftype;
    if (name != NULL) {
        lastfi->name = name;
        b = avltree_insert(&lastfi->node, &file_tree, file_compare);
    } else {
        b = (command_line != NULL) ? &command_line->node : NULL;
        if (command_line == NULL) command_line = lastfi;
    }
    if (b != NULL) {
        free(base2);
        tmp = avltree_container_of(b, struct file_s, node);
    } else { /* new file */
        Encoding_types encoding = E_UNKNOWN;
        FILE *f;
        uchar_t c = 0;
        size_t fp = 0;

        lastfi->line = NULL;
        lastfi->lines = 0;
        lastfi->data = NULL;
        lastfi->len = 0;
        lastfi->open = 0;
        lastfi->err_no = 0;
        lastfi->read_error = false;
        lastfi->portable = false;
        avltree_init(&lastfi->star);
        tmp = lastfi;
        lastfi = NULL;
        if (name != NULL) {
            int err = 1;
            char *path = NULL;
            size_t namelen = strlen(name) + 1;
            s = (char *)mallocx(namelen);
            memcpy(s, name, namelen); tmp->name = s;
            if (val != NULL) {
                struct include_list_s *i = include_list.next;
                f = file_open(name, "rb");
                while (f == NULL && i != NULL) {
                    free(path);
                    path = get_path(val, i->path);
                    f = file_open(path, "rb");
                    i = i->next;
                }
            } else {
                f = dash_name(name) ? stdin : file_open(name, "rb");
            }
            if (path == NULL) {
                s = (char *)mallocx(namelen);
                memcpy(s, name, namelen);
                path = s;
            }
            tmp->realname = path;
            if (arguments.quiet) {
                printf((ftype == 1) ? "Reading file:      " : "Assembling file:   ");
                argv_print(path, stdout);
                putchar('\n');
            }
            if (f == NULL) goto openerr;
            tmp->read_error = true;
            if (f != stdin) setvbuf(f, NULL, _IONBF, BUFSIZ);
            if (ftype == 1) {
                bool check = true;
                if (fseek(f, 0, SEEK_END) == 0) {
                    long len = ftell(f);
                    if (len > 0) {
                        size_t len2 = (size_t)len;
                        tmp->data = (uint8_t *)malloc(len2);
                        if (tmp->data != NULL) tmp->len = len2;
                    }
                    rewind(f);
                }
                clearerr(f); errno = 0;
                if (tmp->len != 0 || !extendfile(tmp)) {
                    for (;;) {
                        fp += fread(tmp->data + fp, 1, tmp->len - fp, f);
                        if (feof(f) == 0 && fp >= tmp->len) {
                            if (check) {
                                int c2 = getc(f);
                                check = false;
                                if (c2 != EOF) {
                                    if (extendfile(tmp)) break;
                                    tmp->data[fp++] = (uint8_t)c2;
                                    continue;
                                }
                            } else {
                                if (extendfile(tmp)) break;
                                continue;
                            }
                        }
                        err = 0;
                        break;
                    }
                }
            } else {
                struct ubuff_s ubuff = {NULL, 0, 0};
                size_t max_lines = 0;
                line_t lines = 0;
                uint8_t buffer[BUFSIZ * 2];
                size_t bp = 0, bl, qr = 1;
                if (fseek(f, 0, SEEK_END) == 0) {
                    long len = ftell(f);
                    if (len > 0) {
                        size_t len2 = (size_t)len + 4096;
                        if (len2 < 4096) len2 = SIZE_MAX; /* overflow */
                        tmp->data = (uint8_t *)malloc(len2);
                        if (tmp->data != NULL) tmp->len = len2;
                        max_lines = (len2 / 20 + 1024) & ~(size_t)1023;
                        if (max_lines > SIZE_MAX / sizeof *tmp->line) max_lines = SIZE_MAX / sizeof *tmp->line; /* overflow */
                        tmp->line = (size_t *)malloc(max_lines * sizeof *tmp->line);
                        if (tmp->line == NULL) max_lines = 0;
                    }
                    rewind(f);
                }
                clearerr(f); errno = 0;
                bl = fread(buffer, 1, BUFSIZ, f);
                if (bl != 0 && buffer[0] == 0) encoding = E_UTF16BE; /* most likely */
#ifdef _WIN32
                setlocale(LC_CTYPE, "");
#endif
                do {
                    size_t k;
                    uint8_t *p;
                    uchar_t lastchar;
                    bool qc = true;
                    uint8_t cclass = 0;

                    if (lines >= max_lines) {
                        size_t *d, len2 = max_lines + 1024;
                        if (/*len2 < 1024 ||*/ len2 > SIZE_MAX / sizeof *tmp->line) goto failed; /* overflow */
                        d = (size_t *)realloc(tmp->line, len2 * sizeof *tmp->line);
                        if (d == NULL) goto failed;
                        tmp->line = d;
                        max_lines = len2;
                    }
                    if ((line_t)(lines + 1) < 1) goto failed; /* overflow */
                    tmp->line[lines++] = fp;
                    p = tmp->data + fp;
                    for (;;) {
                        unsigned int i, j;
                        size_t o = (size_t)(p - tmp->data);
                        uint8_t ch2;
                        if (o + 6*6 + 1 > tmp->len) {
                            if (extendfile(tmp)) goto failed;
                            p = tmp->data + o;
                        }
                        if (bp / (BUFSIZ / 2) == qr) {
                            if (qr == 1) {
                                qr = 3;
                                if (feof(f) == 0) bl = BUFSIZ + fread(buffer + BUFSIZ, 1, BUFSIZ, f);
                            } else {
                                qr = 1;
                                if (feof(f) == 0) bl = fread(buffer, 1, BUFSIZ, f);
                            }
                        }
                        if (bp == bl) break;
                        lastchar = c;
                        c = buffer[bp]; bp = (bp + 1) % (BUFSIZ * 2);
                        if (!arguments.to_ascii) {
                            if (c == 10) {
                                if (lastchar == 13) continue;
                                break;
                            }
                            if (c == 13) {
                                break;
                            }
                            if (c != 0 && c < 0x80) *p++ = (uint8_t)c; else p = utf8out(c, p);
                            continue;
                        }
                        switch (encoding) {
                        case E_UNKNOWN:
                        case E_UTF8:
                            if (c < 0x80) goto done;
                            if (c < 0xc0) {
                            invalid:
                                if (encoding == E_UNKNOWN) {
                                    c = fromiso(c);
                                    encoding = E_ISO; break;
                                }
                                c = REPLACEMENT_CHARACTER; break;
                            } 
                            ch2 = (bp == bl) ? 0 : buffer[bp];
                            if (c < 0xe0) {
                                if (c < 0xc2) goto invalid;
                                c ^= 0xc0; i = 1;
                            } else if (c < 0xf0) {
                                if ((c ^ 0xe0) == 0 && (ch2 ^ 0xa0) >= 0x20) goto invalid;
                                c ^= 0xe0; i = 2;
                            } else if (c < 0xf8) {
                                if ((c ^ 0xf0) == 0 && (uint8_t)(ch2 - 0x90) >= 0x30) goto invalid;
                                c ^= 0xf0; i = 3;
                            } else if (c < 0xfc) {
                                if ((c ^ 0xf8) == 0 && (uint8_t)(ch2 - 0x88) >= 0x38) goto invalid;
                                c ^= 0xf8; i = 4;
                            } else if (c < 0xfe) {
                                if ((c ^ 0xfc) == 0 && (uint8_t)(ch2 - 0x84) >= 0x3c) goto invalid;
                                c ^= 0xfc; i = 5;
                            } else {
                                if (encoding != E_UNKNOWN) goto invalid;
                                if (c == 0xff && ch2 == 0xfe) encoding = E_UTF16LE;
                                else if (c == 0xfe && ch2 == 0xff) encoding = E_UTF16BE;
                                else goto invalid;
                                bp = (bp + 1) % (BUFSIZ * 2);
                                continue;
                            }

                            for (j = i; i != 0; i--) {
                                if (bp != bl) {
                                    ch2 = buffer[bp];
                                    if ((ch2 ^ 0x80) < 0x40) {
                                        c = (c << 6) ^ ch2 ^ 0x80;
                                        bp = (bp + 1) % (BUFSIZ * 2);
                                        continue;
                                    }
                                }
                                if (encoding != E_UNKNOWN) {
                                    c = REPLACEMENT_CHARACTER;break;
                                }
                                encoding = E_ISO;
                                i = (j - i) * 6;
                                qc = false;
                                if (ubuff.p >= ubuff.len && extendbuff(&ubuff)) goto failed;
                                ubuff.data[ubuff.p++] = fromiso(((~0x7f >> j) & 0xff) | (c >> i));
                                for (;i != 0; i-= 6) {
                                    if (ubuff.p >= ubuff.len && extendbuff(&ubuff)) goto failed;
                                    ubuff.data[ubuff.p++] = fromiso(((c >> (i-6)) & 0x3f) | 0x80);
                                }
                                if (bp == bl) goto eof;
                                c = (ch2 >= 0x80) ? fromiso(ch2) : ch2; 
                                j = 0;
                                bp = (bp + 1) % (BUFSIZ * 2);
                                break;
                            }
                            if (j != 0) encoding = E_UTF8;
                            break;
                        case E_UTF16LE:
                            if (bp == bl) goto invalid;
                            c |= (uchar_t)buffer[bp] << 8; bp = (bp + 1) % (BUFSIZ * 2);
                            if (c == 0xfffe) {
                                encoding = E_UTF16BE;
                                continue;
                            }
                            break;
                        case E_UTF16BE:
                            if (bp == bl) goto invalid;
                            c = (c << 8) | buffer[bp]; bp = (bp + 1) % (BUFSIZ * 2);
                            if (c == 0xfffe) {
                                encoding = E_UTF16LE;
                                continue;
                            }
                            break;
                        case E_ISO:
                            if (c >= 0x80) c = fromiso(c);
                            goto done;
                        }
                        if (c == 0xfeff) continue;
                        if (encoding != E_UTF8) {
                            if (c >= 0xd800 && c < 0xdc00) {
                                if (lastchar < 0xd800 || lastchar >= 0xdc00) continue;
                                c = REPLACEMENT_CHARACTER;
                            } else if (c >= 0xdc00 && c < 0xe000) {
                                if (lastchar >= 0xd800 && lastchar < 0xdc00) {
                                    c ^= 0x360dc00 ^ (lastchar << 10);
                                    c += 0x10000;
                                } else c = REPLACEMENT_CHARACTER;
                            } else if (lastchar >= 0xd800 && lastchar < 0xdc00) {
                                c = REPLACEMENT_CHARACTER;
                            }
                        }
                    done:
                        if (c < 0xc0) {
                            if (c == 10) {
                                if (lastchar == 13) continue;
                                break;
                            }
                            if (c == 13) {
                                break;
                            }
                            cclass = 0;
                            if (!qc) {
                                unfc(&ubuff);
                                qc = true;
                            }
                            if (ubuff.p == 1) {
                                if (ubuff.data[0] != 0 && ubuff.data[0] < 0x80) *p++ = (uint8_t)ubuff.data[0]; else p = utf8out(ubuff.data[0], p);
                            } else {
                                p = flushubuff(&ubuff, p, tmp);
                                if (p == NULL) goto failed;
                                ubuff.p = 1;
                            }
                            ubuff.data[0] = c;
                        } else {
                            const struct properties_s *prop = uget_property(c);
                            uint8_t ncclass = prop->combclass;
                            if ((ncclass != 0 && cclass > ncclass) || (prop->property & (qc_N | qc_M)) != 0) {
                                qc = false;
                                if (ubuff.p >= ubuff.len && extendbuff(&ubuff)) goto failed;
                                ubuff.data[ubuff.p++] = c;
                            } else {
                                if (!qc) {
                                    unfc(&ubuff);
                                    qc = true; 
                                }
                                if (ubuff.p == 1) {
                                    if (ubuff.data[0] != 0 && ubuff.data[0] < 0x80) *p++ = (uint8_t)ubuff.data[0]; else p = utf8out(ubuff.data[0], p);
                                } else {
                                    p = flushubuff(&ubuff, p, tmp);
                                    if (p == NULL) goto failed;
                                    ubuff.p = 1;
                                }
                                ubuff.data[0] = c;
                            }
                            cclass = ncclass;
                        }
                    }
                eof:
                    if (!qc) unfc(&ubuff);
                    p = flushubuff(&ubuff, p, tmp);
                    if (p == NULL) goto failed;
                    ubuff.p = 0;
                    k = (size_t)(p - tmp->data) - fp;
                    p = tmp->data + fp;
                    while (k != 0 && (p[k-1]==' ' || p[k-1]=='\t')) k--;
                    p[k++] = 0;
                    fp += k;
                } while (bp != bl);
                err = 0;
            failed:
#ifdef _WIN32
                setlocale(LC_CTYPE, "C");
#endif
                free(ubuff.data);
                tmp->lines = lines;
                if (lines != max_lines) {
                    size_t *d = (size_t *)realloc(tmp->line, lines * sizeof *tmp->line);
                    if (lines == 0 || d != NULL) tmp->line = d;
                }
            }
            tmp->len = fp;
            if (fp == 0) {
                free(tmp->data);
                tmp->data = NULL;
            } else {
                uint8_t *d = (uint8_t *)realloc(tmp->data, fp);
                if (d != NULL) tmp->data = d;
            }
            if (err != 0) errno = ENOMEM;
            err |= ferror(f);
            if (f != stdin) err |= fclose(f);
        openerr:
            if (err != 0 && errno != 0) {
                tmp->err_no = errno;
                free(tmp->data);
                tmp->data = NULL;
                tmp->len = 0;
                free(tmp->line);
                tmp->line = NULL;
                tmp->lines = 0;
            }
        } else {
            const char *cmd_name = "<command line>";
            size_t cmdlen = strlen(cmd_name) + 1;
            s = (char *)mallocx(1);
            s[0] = 0; tmp->name = s;
            s = (char *)mallocx(cmdlen);
            memcpy(s, cmd_name, cmdlen); tmp->realname = s;
        }

        tmp->uid = (ftype != 1) ? curfnum++ : 0;
        tmp->encoding = encoding;
    }
    if (tmp->err_no != 0) {
        char *path = (val != NULL) ? get_path(val, "") : NULL;
        errno = tmp->err_no;
        err_msg_file(tmp->read_error ? ERROR__READING_FILE : ERROR_CANT_FINDFILE, (val != NULL) ? path : name, epoint);
        free(path);
        return NULL;
    } 
    if (!tmp->portable && val != NULL && diagnostics.portable) {
        tmp->portable = portability(val, epoint);
    }
    tmp->open++;
    return tmp;
}

void closefile(struct file_s *f) {
    if (f->open != 0) f->open--;
}

static struct stars_s {
    struct star_s stars[255];
    struct stars_s *next;
} *stars = NULL;

static struct star_s *lastst;
static int starsp;
struct star_s *new_star(line_t line, bool *exists) {
    struct avltree_node *b;
    struct star_s *tmp;
    lastst->line = line;
    b = avltree_insert(&lastst->node, star_tree, star_compare);
    if (b == NULL) { /* new label */
	*exists = false;
        avltree_init(&lastst->tree);
        if (starsp == 254) {
            struct stars_s *old = stars;
            stars = (struct stars_s *)mallocx(sizeof *stars);
            stars->next = old;
            starsp = 0;
        } else starsp++;
        tmp = lastst;
        lastst = &stars->stars[starsp];
	return tmp;
    }
    *exists = true;
    return avltree_container_of(b, struct star_s, node);            /* already exists */
}

void destroy_file(void) {
    struct stars_s *old;

    avltree_destroy(&file_tree, file_free);
    free(lastfi);
    if (command_line != NULL) file_free(&command_line->node);

    include_list_last = include_list.next;
    while (include_list_last != NULL) {
        struct include_list_s *tmp = include_list_last;
        include_list_last = tmp->next;
        free(tmp);
    }

    while (stars != NULL) {
        old = stars;
        stars = stars->next;
        free(old);
    }
}

void init_file(void) {
    avltree_init(&file_tree);
    stars = (struct stars_s *)mallocx(sizeof *stars);
    stars->next = NULL;
    starsp = 0;
    lastst = &stars->stars[starsp];
}

void makefile(int argc, char *argv[]) {
    FILE *f;
    char *path;
    struct linepos_s nopoint = {0, 0};
    struct avltree_node *n;
    size_t len = 0;
    int i, err;

    f = dash_name(arguments.make) ? stdout : file_open(arguments.make, "wt");
    if (f == NULL) {
        err_msg_file(ERROR_CANT_WRTE_MAK, arguments.make, &nopoint);
        return;
    }
    clearerr(f); errno = 0;
    if (!dash_name(arguments.output.name)) {
        path = get_path(NULL, arguments.output.name);
        len += argv_print(arguments.output.name + strlen(path), f);
        free(path);
    }
    if (len > 0) {
        len++;
        putc(':', f);

        for (i = 0; i < argc; i++) {
            if (dash_name(argv[i])) continue;
            if (len > 64) {
                fputs(" \\\n", f);
                len = 0;
            }
            putc(' ', f);
            len += argv_print(argv[i], f) + 1;
        }

        for (n = avltree_first(&file_tree); n != NULL; n = avltree_next(n)) {
            const struct file_s *a = cavltree_container_of(n, struct file_s, node);
            if (a->type != 0) {
                if (len > 64) {
                    fputs(" \\\n", f);
                    len = 0;
                }
                putc(' ', f);
                len += argv_print(a->realname, f) + 1;
            }
        }
        putc('\n', f);
    }

    err = ferror(f);
    err |= (f != stdout) ? fclose(f) : fflush(f);
    if (err != 0 && errno != 0) err_msg_file(ERROR_CANT_WRTE_MAK, arguments.make, &nopoint);
}
