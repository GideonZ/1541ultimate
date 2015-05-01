/*
 * filetypes.cc
 *
 *  Created on: Apr 25, 2015
 *      Author: Gideon
 */

#include "filetypes.h"

/* Global Instance of file type factory */
FileTypeFactory file_type_factory;

FileType * FileTypes :: attempt_promotion(void)
{
    FileDirEntry *promoted = file_type_factory.promote(this);
    if(promoted) {
        printf("Promotion success! %s\n", promoted->get_name());
        parent->children.replace(this, promoted); // replace
		detach(true);
		promoted->attach(true);
		this->info = NULL; // FIXME: prevent the file info to be destroyed, since we copied it to this new FileDirEntry! (dirty)
		push_event(e_cleanup_path_object, this);
    }
    return promoted;
}



