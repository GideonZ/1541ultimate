#include "filetype_t64.h"
#include "directory.h"
#include "filemanager.h"
#include "t64_filesystem.h"

extern "C" {
	#include "dump_hex.h"
}

// tester instance
FactoryRegistrator<CachedTreeNode *, FileType *> tester_t64(Globals :: getFileTypeFactory(), FileTypeT64 :: test_type);

/*********************************************************************/
/* T64 File Browser Handling                                 */
/*********************************************************************/

FileTypeT64 :: FileTypeT64(CachedTreeNode *n)
{
	node = n;
	printf("Creating T64 type from info: %s\n", n->get_name());
    // we'll create a file-mapped filesystem here and attach the T64 file system
    fs  = new FileSystemT64(NULL, n);
}

FileTypeT64 :: ~FileTypeT64()
{
	if(fs)
		delete fs;
}

int   FileTypeT64 :: fetch_context_items(IndexedList<Action *> &list)
{
	return 0;
}

FileType *FileTypeT64 :: test_type(CachedTreeNode *obj)
{
	FileInfo *inf = obj->get_file_info();
    if(strcmp(inf->extension, "T64")==0)
        return new FileTypeT64(obj);
    return NULL;
}

