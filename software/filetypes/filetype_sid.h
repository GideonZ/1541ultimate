#ifndef FILETYPE_SID_H
#define FILETYPE_SID_H

#include "filetypes.h"
#include "browsable_root.h"

class FileTypeSID : public FileType
{
	FileManager *fm;
	SubsysCommand *cmd;
	char ssl_filename[92];
	mstring path_string;
	mstring file_string;

	File *file;
	uint8_t sid_header[0x80];
	int header_version;
	uint16_t song;
	uint16_t default_song;
	uint16_t start;
	uint16_t end;
	uint16_t header_location;
	uint32_t data_offset;
	uint16_t flags;
	bool header_valid;
	int number_of_songs;
	bool mus_file;
	bool sid_file;

	int prepare(bool);
	void load(void);
	int loadFile(File *file, int offset);
	bool tryLoadStereoMus(int offset);
    int execute(SubsysCommand *cmd);
    static int execute_st(SubsysCommand *cmd);
    int readHeader(void);
	void processHeader(void);
	int createMusHeader(void);
    void showInfo(void);
    void readSongLengths(void);
	void configureMusEnv(int offsetLoadEnd);
    bool ConfigSIDs(void);
public:
    FileTypeSID(BrowsableDirEntry *n);
	FileTypeSID(const char *filename, const char *sslfile, bool mus);

    ~FileTypeSID();

    int fetch_context_items(IndexedList<Action *> &list);
    static FileType *test_type(BrowsableDirEntry *obj);
	static int play_file(const char *filename, const char *sslfile, int song);
};

#endif
