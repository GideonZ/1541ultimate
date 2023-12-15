#ifndef ASSEMBLY_H
#define ASSEMBLY_H

#include "json.h"
extern "C" {
    #include "server.h"
    #include "multipart.h"
}

typedef struct {
    int offset;
    int size;
    uint8_t buffer[16384];
} t_BufferedBody;

class Assembly
{
    char *assembly_presets =
        "[{\"type\":\"date\",\"values\":[{\"aqlKey\":\"1980\"},{\"aqlKey\":\"1981\"},{\"aqlKey\":\"1982\"},{\"aqlKey\":"
        "\"1983\"},{\"aqlKey\":\"1984\"},{\"aqlKey\":\"1985\"},{\"aqlKey\":\"1986\"},{\"aqlKey\":\"1987\"},{\"aqlKey\":"
        "\"1988\"},{\"aqlKey\":\"1989\"},{\"aqlKey\":\"1990\"},{\"aqlKey\":\"1991\"},{\"aqlKey\":\"1992\"},{\"aqlKey\":"
        "\"1993\"},{\"aqlKey\":\"1994\"},{\"aqlKey\":\"1995\"},{\"aqlKey\":\"1996\"},{\"aqlKey\":\"1997\"},{\"aqlKey\":"
        "\"1998\"},{\"aqlKey\":\"1999\"},{\"aqlKey\":\"2000\"},{\"aqlKey\":\"2001\"},{\"aqlKey\":\"2002\"},{\"aqlKey\":"
        "\"2003\"},{\"aqlKey\":\"2004\"},{\"aqlKey\":\"2005\"},{\"aqlKey\":\"2006\"},{\"aqlKey\":\"2007\"},{\"aqlKey\":"
        "\"2008\"},{\"aqlKey\":\"2009\"},{\"aqlKey\":\"2010\"},{\"aqlKey\":\"2011\"},{\"aqlKey\":\"2012\"},{\"aqlKey\":"
        "\"2013\"},{\"aqlKey\":\"2014\"},{\"aqlKey\":\"2015\"},{\"aqlKey\":\"2016\"},{\"aqlKey\":\"2017\"},{\"aqlKey\":"
        "\"2018\"},{\"aqlKey\":\"2019\"},{\"aqlKey\":\"2020\"},{\"aqlKey\":\"2021\"},{\"aqlKey\":\"2022\"},{\"aqlKey\":"
        "\"2023\"}]},{\"type\":\"rating\",\"values\":[{\"aqlKey\":\"1\"},{\"aqlKey\":\"2\"},{\"aqlKey\":\"3\"},{"
        "\"aqlKey\":\"4\"},{\"aqlKey\":\"5\"},{\"aqlKey\":\"6\"},{\"aqlKey\":\"7\"},{\"aqlKey\":\"8\"},{\"aqlKey\":"
        "\"9\"},{\"aqlKey\":\"10\"}]},{\"type\":\"type\",\"values\":[{\"aqlKey\":\"bin\"},{\"aqlKey\":\"crt\"},{"
        "\"aqlKey\":\"d64\"},{\"aqlKey\":\"d71\"},{\"aqlKey\":\"d81\"},{\"aqlKey\":\"g64\"},{\"aqlKey\":\"prg\"},{"
        "\"aqlKey\":\"sid\"},{\"aqlKey\":\"t64\"},{\"aqlKey\":\"tap\"}]},{\"type\":\"repo\",\"values\":[{\"aqlKey\":"
        "\"csdb\",\"name\":\"CSDB games\"},{\"aqlKey\":\"tapes\",\"name\":\"c64tapes.org "
        "tapes\"},{\"aqlKey\":\"tapesno\",\"name\":\"c64.no tapes\"},{\"aqlKey\":\"c64comgames\",\"name\":\"c64.com "
        "games\"},{\"aqlKey\":\"gamebase\",\"name\":\"Gamebase64\"},{\"aqlKey\":\"seuck\",\"name\":\"Seuck "
        "games\"},{\"aqlKey\":\"mayhem\",\"name\":\"Mayhem crt\"},{\"aqlKey\":\"pres\",\"name\":\"Preservers "
        "discs\"},{\"aqlKey\":\"guybrush\",\"name\":\"Guybrush games\"},{\"aqlKey\":\"c64comdemos\",\"name\":\"c64.com "
        "demos\"},{\"aqlKey\":\"hvsc\",\"name\":\"HVSC Music\"},{\"aqlKey\":\"c64orgintro\",\"name\":\"c64.org "
        "intros\"}]},{\"type\":\"category\",\"values\":[{\"aqlKey\":\"games\",\"name\":\"Games\"},{\"aqlKey\":"
        "\"demos\",\"name\":\"Demos\"},{\"aqlKey\":\"music\",\"name\":\"Music\"},{\"aqlKey\":\"graphics\",\"name\":"
        "\"Graphics\"},{\"aqlKey\":\"mags\",\"name\":\"Mags\"},{\"aqlKey\":\"tools\",\"name\":\"Tools\"},{\"aqlKey\":"
        "\"c128\",\"name\":\"C128\"},{\"aqlKey\":\"misc\",\"name\":\"Misc\"},{\"aqlKey\":\"intros\",\"name\":"
        "\"Intros\"},{\"aqlKey\":\"charts\",\"name\":\"Charts\"},{\"aqlKey\":\"reu\",\"name\":\"Reu\"},{\"aqlKey\":"
        "\"easyflash\",\"name\":\"Easyflash\"},{\"aqlKey\":\"bbs\",\"name\":\"BBS\"}]},{\"type\":\"subcat\",\"values\":"
        "[{\"id\":0,\"aqlKey\":\"games\",\"name\":\"CSDB "
        "games\"},{\"id\":12,\"aqlKey\":\"tapes\",\"name\":\"C64tapes.org "
        "tapes\"},{\"id\":13,\"aqlKey\":\"tapesno\",\"name\":\"C64.no "
        "tapes\"},{\"id\":15,\"aqlKey\":\"c64comgames\",\"name\":\"C64.com "
        "games\"},{\"id\":16,\"aqlKey\":\"gamebase\",\"name\":\"Gamebase64\"},{\"id\":17,\"aqlKey\":\"seuck\",\"name\":"
        "\"Seuck games\"},{\"id\":22,\"aqlKey\":\"mayhem\",\"name\":\"Mayhem "
        "crt\"},{\"id\":23,\"aqlKey\":\"presdisk\",\"name\":\"Preservers "
        "discs\"},{\"id\":24,\"aqlKey\":\"prestap\",\"name\":\"Preservers "
        "taps\"},{\"id\":26,\"aqlKey\":\"guybrushgames\",\"name\":\"Guybrush "
        "games\"},{\"id\":1,\"aqlKey\":\"demos\",\"name\":\"CSDB "
        "demos\"},{\"id\":14,\"aqlKey\":\"c64comdemos\",\"name\":\"C64.com "
        "demos\"},{\"id\":31,\"aqlKey\":\"guybrushdemos\",\"name\":\"Guybrush "
        "Demos\"},{\"id\":32,\"aqlKey\":\"mibridemos\",\"name\":\"Mibri "
        "demoshow\"},{\"id\":4,\"aqlKey\":\"music\",\"name\":\"CSDB "
        "music\"},{\"id\":18,\"aqlKey\":\"hvscmusic\",\"name\":\"HVSC "
        "Music\"},{\"id\":19,\"aqlKey\":\"hvscgames\",\"name\":\"HVSC "
        "games\"},{\"id\":20,\"aqlKey\":\"hvscdemos\",\"name\":\"HVSC "
        "demos\"},{\"id\":3,\"aqlKey\":\"graphics\",\"name\":\"CSDB "
        "graphics\"},{\"id\":5,\"aqlKey\":\"discmags\",\"name\":\"CSDB "
        "discmags\"},{\"id\":8,\"aqlKey\":\"tools\",\"name\":\"CSDB "
        "tools\"},{\"id\":27,\"aqlKey\":\"guybrushutils\",\"name\":\"Guybrush "
        "Utils\"},{\"id\":28,\"aqlKey\":\"guybrushutilsgerman\",\"name\":\"Guybrush Utils "
        "German\"},{\"id\":2,\"aqlKey\":\"c128stuff\",\"name\":\"CSDB "
        "128\"},{\"id\":7,\"aqlKey\":\"c64misc\",\"name\":\"CSDB "
        "misc\"},{\"id\":29,\"aqlKey\":\"guybrushgamesgerman\",\"name\":\"Guybrush Games "
        "German\"},{\"id\":30,\"aqlKey\":\"guybrushmisc\",\"name\":\"Guybrush "
        "Misc\"},{\"id\":11,\"aqlKey\":\"intros\",\"name\":\"C64.org "
        "intros\"},{\"id\":9,\"aqlKey\":\"charts\",\"name\":\"CSDB "
        "charts\"},{\"id\":25,\"aqlKey\":\"reu\",\"name\":\"CSDB "
        "reu\"},{\"id\":10,\"aqlKey\":\"easyflash\",\"name\":\"CSDB "
        "easyflash\"},{\"id\":6,\"aqlKey\":\"bbs\",\"name\":\"CSDB bbs\"}]}]";

    char *presets_text;
    JSON *presets;
    int socket_fd;
    HTTPReqMessage response;

    int read_socket(void);
    void  get_response(HTTPREQ_CALLBACK callback);
    JSON *convert_buffer_to_json(t_BufferedBody *body);
public:
    Assembly() {
        presets = NULL;
        presets_text = new char[1+strlen(assembly_presets)];
        strcpy(presets_text, assembly_presets);
        convert_text_to_json_objects(presets_text, strlen(presets_text), 1000, &presets);
    }
    ~Assembly() {
        delete[] presets_text;
        if (presets)
            delete presets;
    }
    void *get_user_context() { return response.userContext; }
    JSON *get_presets(void) { return presets; }
    JSON *send_query(const char *query);
    JSON *request_entries(const char *id, int cat);
    void  request_binary(const char *id, int cat, int idx);
    int   connect_to_server(void);
    void  close_connection(void);
};

extern Assembly assembly;

#endif // ASSEMBLY_H