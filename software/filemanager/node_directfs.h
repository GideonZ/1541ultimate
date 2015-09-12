/*
 * node_directfs.h
 *
 *  Created on: Sep 12, 2015
 *      Author: Gideon
 */

#ifndef FILEMANAGER_NODE_DIRECTFS_H_
#define FILEMANAGER_NODE_DIRECTFS_H_

#include "cached_tree_node.h"

class Node_DirectFS : public CachedTreeNode
{
	FileSystem *fs;
public:
	Node_DirectFS(FileSystem *fs, const char *n) : CachedTreeNode(NULL, n) {
		this->fs = fs;
		info.fs = fs;
		info.attrib = AM_DIR;
	}
    virtual ~Node_DirectFS() {

    }

	bool is_ready(void) {
		return true;
	}
    int  probe(void) {
    	return 1;
    }
};


#endif /* FILEMANAGER_NODE_DIRECTFS_H_ */
