#include "iec_ui.h"
#include "form.h"
#include "tree_browser.h"

IecFileSystem *BrowsableIecNewPartition::getIecFileSystem()
{
    BrowsableIecPartitions *parent = (BrowsableIecPartitions *)collection;
    return parent->getIecFileSystem();
}
TreeBrowser *BrowsableIecNewPartition::getBrowser()
{
    BrowsableIecPartitions *parent = (BrowsableIecPartitions *)collection;
    return parent->getBrowser();
}

SubsysResultCode_e BrowsableIecPartition::S_edit(SubsysCommand *cmd)
{
    BrowsableIecPartition *bp = (BrowsableIecPartition *)cmd->direct_obj;
    IecFileSystem *vfs = bp->getIecFileSystem();
    JSON_Object *form_fields = JSON::Obj()->add("Number", bp->num)->add("Name", bp->name.c_str())->add("Path", bp->path.c_str());
    UserInterface *ui = cmd->user_interface;

    FormUI *form = new FormUI(ui, 38, 9, "Edit Existing Partition", form_fields);
    form->init(ui->screen, ui->keyboard);

    do {
        int ret = ui->uiobject_modal(form);

        if (ret == MENU_DONE) {
            const char *path = form_fields->string_or("Path", "");
            const int part = form_fields->int_or("Number", 1);
            if (!*path) {
                ui->popup("Path cannot be empty", BUTTON_OK);
                continue;
            }
            if ((part < 1)||(part >= MAX_PARTITIONS)) {
                ui->popup("Part. # should be between 1-255", BUTTON_OK);
                continue;
            }
            if (part != bp->num) { // move parition to another slot
                if (vfs->GetPartition(part)) {
                    ui->popup("Partition number already in use", BUTTON_OK);
                    continue;
                }
                vfs->RemovePartition(bp->num);
            }
            // OK!
            vfs->add_partition(part, path, form_fields->string_or("Name", "NO NAME"));
            bp->getBrowser()->state->needs_reload = true;
            bp->getBrowser()->state->refresh = true;
            break;
        } else { // some other code
            break;
        } 
    } while(1);
    // Cleanup
    form->deinit();
    delete form;
    return SSRET_OK;
}

SubsysResultCode_e BrowsableIecPartition::S_remove(SubsysCommand *cmd)
{
    BrowsableIecPartition *bp = (BrowsableIecPartition *)cmd->direct_obj;
    IecFileSystem *vfs = bp->getIecFileSystem();
    bp->getBrowser()->state->needs_reload = true;
    bp->getBrowser()->state->refresh = true;
    vfs->RemovePartition(bp->num);
    return SSRET_OK;
}

SubsysResultCode_e BrowsableIecNewPartition::S_add(SubsysCommand *cmd)
{
    printf("Add new partition..\n");
    BrowsableIecNewPartition *bp = (BrowsableIecNewPartition *)cmd->direct_obj;

    mstring new_path;
    if (!pick_path(cmd->user_interface, new_path)) {
        return SSRET_ABORTED_BY_USER;
    }
    IecFileSystem *vfs = bp->getIecFileSystem();
    JSON_Object *form_fields = JSON::Obj()
        ->add("Number", vfs->GetUnusedPartition())
        ->add("Name", "NO NAME")
        ->add("Path", new_path.c_str());

    
    if (form_new_partition(cmd->user_interface, form_fields) == MENU_DONE) {
        vfs->add_partition(form_fields->int_or("Number", 99), form_fields->string_or("Path", "/"), form_fields->string_or("Name", "NO NAME"));
        bp->getBrowser()->state->needs_reload = true;
        bp->getBrowser()->state->refresh = true;
    }
    return SSRET_OK;
}

void show_partitions(UserInterface *ui, IecFileSystem *vfs)
{
    BrowsableIecPartitions *root = new BrowsableIecPartitions(vfs);
    TreeBrowser *pt = new TreeBrowser(ui, root);
    root->setBrowser(pt);
    pt->has_border = true;
    pt->allow_exit = true;
    pt->allow_tasks = false;
    pt->setCleanup();
    pt->init();
    ui->activate_uiobject(pt);
}

// called from GUI task
int form_new_partition(UserInterface *ui, JSON_Object *form_fields)
{ 
    FormUI *form = new FormUI(ui, 38, 9, "Enter New IEC Partition", form_fields);
    form->init(ui->screen, ui->keyboard);

    // struct set_part_t *data;
    // iec_closure_t cb;

    int ret = 0;
    do {
        ret = ui->uiobject_modal(form);

        if (ret == MENU_DONE) {
            const char *path = form_fields->string_or("Path", "");
            const int part = form_fields->int_or("Number", 1);
            if (!*path) {
                ui->popup("Path cannot be empty", BUTTON_OK);
                continue;
            }
            if ((part < 1)||(part >= MAX_PARTITIONS)) {
                ui->popup("Part. # should be between 1-255", BUTTON_OK);
                continue;
            }
            // OK!
            // This is the way it should be, but for now we bypass it
            // data = new struct set_part_t;
            // *data = {
            //     form_fields->int_or("Number", 1),
            //     strdup(form_fields->string_or("Path", "/")),  // duplicate here, because frun_from_iec frees them
            //     strdup(form_fields->string_or("Name", "NO NAME"))
            // };
            // cb = { drive, IecDrive :: set_iec_dir, data };
            // intf->run_from_iec(&cb);
            break;
        } else { // some other code
            break;
        } 
    } while(1);
    // Cleanup
    form->deinit();
    delete form;
    return ret;
}
