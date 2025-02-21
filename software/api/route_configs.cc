#include "routes.h"
#include "pattern.h"
#include "config.h"
#include "attachment_writer.h"

void emit_store(ConfigStore *st, JSON_Object *pobj, ArgsURI &args)
{
    JSON_Object *sobj = JSON::Obj();
    pobj->add(st->get_store_name(), sobj);

    ConfigItem *i;

    for(int n = 0; n < st->items.get_elements(); n++) {
        i = st->items[n];
        if ((args.get_path_depth() < 2) || (pattern_match(args.get_path(1), i->definition->item_text))) {
            JSON *obj;            
            if ((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRFUNC) || (i->definition->type == CFG_TYPE_STRPASS)) {
                obj = new JSON_String(i->getString());
            } else if(i->definition->type == CFG_TYPE_ENUM) {
                obj = new JSON_String(i->definition->items[i->getValue()]);
            } else if(i->definition->type == CFG_TYPE_VALUE) {
                obj = new JSON_Integer(i->getValue());
            } else {
                continue;
            }
            if (args.get_path_depth() < 2) {
                sobj->add(i->definition->item_text, obj);
            } else {
                JSON_Object *ob;
                sobj->add(i->definition->item_text, ob = JSON::Obj() -> add("current", obj));
                if (i->definition->type == CFG_TYPE_ENUM) {
                    JSON_List *list = JSON::List();
                    ob->add("values", list);
                    for(int j=0;j <= i->definition->max; j++) {
                        list->add(i->definition->items[j]);
                    }
                    ob->add("default", i->definition->items[i->definition->def]);
                } else if (i->definition->type == CFG_TYPE_VALUE) {
                    ob->add("min", i->definition->min);
                    ob->add("max", i->definition->max);
                    ob->add("format", (const char *)i->definition->item_format);
                    ob->add("default", (int)i->definition->def);
                } else if((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRFUNC) || (i->definition->type == CFG_TYPE_STRPASS)) {
                    ob->add("default", (const char *)i->definition->def);
                }
            }
        }
    }
}

bool set_item(ResponseWrapper *resp, ConfigItem *item, const char *valuestr)
{
    bool found = false;
    int value;
    if (item->definition->type == CFG_TYPE_VALUE) {
        int value = strtol(valuestr, NULL, 0);
        if (value < item->definition->min || value > item->definition->max) {
            resp->error("Value %d is outside of the allowable range (%d-%d)", value, item->definition->min, item->definition->max);
            return false;
        }
        item->setValue(value);
    } else if ((item->definition->type == CFG_TYPE_STRING) || (item->definition->type == CFG_TYPE_STRFUNC) || (item->definition->type == CFG_TYPE_STRPASS)) {
        item->setString(valuestr);
    } else if (item->definition->type == CFG_TYPE_ENUM) {
        // this is the most nasty one. Let's just iterate over the possibilities and compare the resulting strings
        for(int n = item->definition->min; n <= item->definition->max; n++) {
            if (strcasecmp(valuestr, item->definition->items[n]) == 0) {
                item->setValue(n);
                found = true;
                break;
            }
        }
        if (!found) {
            resp->error("Value '%s' is not a valid choice for item %s", valuestr, item->definition->item_text);
            return false;
        }
    }
    return true;
}

bool apply_store(ResponseWrapper *resp, ConfigStore *store, JSON_Object *obj)
{
    IndexedList<const char *> *keys = obj->get_keys();
    IndexedList<JSON *> *values = obj->get_values();
    bool success = true;
    for (int i=0; i < keys->get_elements(); i++) {
        // now look for the store element with the itemname
        ConfigItem *item = store->find_item((*keys)[i]);
        if (!item) {
            resp->error("'%s' is not a valid item name in category '%s'.", (*keys)[i], store->get_store_name());
            success = false;
            continue;
        }
        // item is valid
        JSON *itemvalue = (*values)[i];

        if ((itemvalue->type() == eObject) || (itemvalue->type() == eList)) {
            resp->error("Value given for '%s' should be a literal.", (*keys)[i]);
            success = false;
            continue;
        }
        if (itemvalue->type() == eBool) {
            resp->error("Value given for '%s' cannot be of type boolean.", (*keys)[i]);
            success = false;
            continue;
        }
        const char *valuestr;
        if (itemvalue->type() == eString) {
            valuestr = ((JSON_String *)itemvalue)->get_string();
        } else {
            valuestr = itemvalue->render();
        }
        success &= set_item(resp, item, valuestr);
    }
    return success;
}

bool apply_config(ResponseWrapper *resp, JSON *obj)
{
    if (obj->type() != eObject) {
        resp->error("Root is not an object.");
        return false;
    }
    ConfigManager *cm = ConfigManager::getConfigManager();
    JSON_Object *root = (JSON_Object *)obj;
    IndexedList<const char *> *keys = root->get_keys();
    IndexedList<JSON *> *values = root->get_values();
    bool success = true;
    for (int i=0; i < keys->get_elements(); i++) {
        ConfigStore *store = cm->find_store((*keys)[i]);
        if (!store) {
            resp->error("'%s' is not a valid configuration category name.", (*keys)[i]);
            success = false;
            continue;
        }
        // store is valid
        JSON *items = (*values)[i];
        if (items->type() != eObject) {
            resp->error("Element '%s' should be an object.", (*keys)[i]);
            success = false;
            continue;
        }
        // store is valid and items is an object
        JSON_Object *itemsObj = (JSON_Object *)items;
        success &= apply_store(resp, store, itemsObj);
        store->at_close_config();
    }
    return success;
}

API_CALL(GET, configs, none, NULL, ARRAY ( { } ))
{
    ConfigManager *cfg = ConfigManager::getConfigManager();
    IndexedList<ConfigStore *> *stores = cfg->getStores();
    int path_elements = args.get_path_depth();

    if (path_elements == 0) { // nothing specified, just return categories
        JSON_List *list = JSON::List();
        resp->json->add("categories", list);
        for(int i=0; i<stores->get_elements(); i++) {
            ConfigStore *s = (*stores)[i];
            list->add(s->get_store_name());
        }
    } else {  // path specified, so the output would list the stores that match the path
        for(int i=0; i<stores->get_elements(); i++) {
            ConfigStore *s = (*stores)[i];
            if ((path_elements < 1) || pattern_match(args.get_path(0), s->get_store_name())) {
                emit_store(s, resp->json, args);
            }
        }
    }

    resp->json_response(HTTP_OK);
}

API_CALL(PUT, configs, none, NULL, ARRAY ( { {"value", P_REQUIRED }} ))
{
    ConfigManager *cfg = ConfigManager::getConfigManager();
    IndexedList<ConfigStore *> *stores = cfg->getStores();
    int path_elements = args.get_path_depth();

    const char *valuestr = args["value"];
    if (valuestr && (path_elements != 2)) {
        resp->error("When using the 'value' argument, the path should have two elements; the config category and the "
                    "config item name. Given: '%s'",
                    args.get_full_path());
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    } else if (!valuestr && (path_elements != 3)) {
        resp->error("When not using the 'value' argument, the path should have three elements; the config category and "
                    "the config item name, followed by the setting. Given: '%s'",
                    args.get_full_path());
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (!valuestr) {
        valuestr = args.get_path(2);
    }
    bool found = false;
    ConfigStore *st;
    for(int i=0; i<stores->get_elements(); i++) {
        st = (*stores)[i];
        if (pattern_match(args.get_path(0), st->get_store_name())) {
            found = true;
            break; // found!
        }
    }

    if (!found) {
        resp->error("There is no config category that matches '%s'", args.get_path(0));
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    ConfigItem *item;
    found = false;
    for(int n = 0; n < st->items.get_elements(); n++) {
        item = st->items[n];
        if (pattern_match(args.get_path(1), item->definition->item_text)) {
            found = true;
            break;
        }
    }
    if (!found) {
        resp->error("Item '%s' not found in this configuration category [%s].", args.get_path(1), st->get_store_name());
        resp->json_response(HTTP_NOT_FOUND);
        return;
    }

    // now we know what item should be configured, and how to 'read' the string
    if (set_item(resp, item, valuestr)) {
        st->at_close_config();
        resp->json_response(HTTP_OK);
    } else {
        resp->json_response(HTTP_BAD_REQUEST);
    }
}

API_CALL(POST, configs, none, &attachment_writer, ARRAY ( { } ))
{
    if (strcasecmp(req->ContentType, "application/json") != 0)  {
        resp->error("Content type should be 'application/json'.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    TempfileWriter *handler = (TempfileWriter *)body;
    if (handler->buffer_file(0, 32768) != 0) {
        resp->error("Could not buffer attachment.");
        resp->json_response(HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    JSON *obj = NULL;
    char *text = (char *)handler->get_buffer(0);
    int tokens = convert_text_to_json_objects(text, handler->get_filesize(0), 1024, &obj);
    if (tokens < 0) {
        resp->error("JSON content could not be parsed. Error: %d", tokens);
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    if (apply_config(resp, obj)) {
        resp->json_response(HTTP_OK);
    } else {
        resp->json_response(HTTP_BAD_REQUEST);
    }
    //resp->json->add("parsed", obj); // now it's owned by the reply! no need to clean up
    delete obj;
}


API_CALL(PUT, configs, load_from_flash, NULL, ARRAY ( {  } ))
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    IndexedList<ConfigStore *> *stores = cm->getStores();
    int path_elements = args.get_path_depth();
    if (path_elements > 1) {
        resp->error("Path depth exceeds 1.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    JSON_List *list = NULL;
    if (path_elements == 1) {
        list = JSON::List();
        resp->json->add("loaded", list);
    }
    for(int n = 0; n < (*stores).get_elements();n++) {
        s = (*stores)[n];
        if ((path_elements == 0) || (pattern_match(args.get_path(0), s->get_store_name()))) {
            s->read(false);
            s->at_close_config();
            if (path_elements == 1) {
                list->add(s->get_store_name());
            }
        }
    }
    resp->json_response(HTTP_OK);
}

API_CALL(PUT, configs, save_to_flash, NULL, ARRAY ( {  } ))
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    IndexedList<ConfigStore *> *stores = cm->getStores();
    int path_elements = args.get_path_depth();

    if (path_elements > 1) {
        resp->error("Path depth exceeds 1.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    JSON_List *list = NULL;
    list = JSON::List();
    resp->json->add("written", list);

    for(int n = 0; n < (*stores).get_elements();n++) {
        s = (*stores)[n];
        if ((path_elements == 0) || (pattern_match(args.get_path(0), s->get_store_name()))) {
            if (s->is_flash_stale()) {
                s->write();
                list->add(s->get_store_name());
            }
        }
    }
    resp->json_response(HTTP_OK);
}

API_CALL(PUT, configs, reset_to_default, NULL, ARRAY ( {  } ))
{
    ConfigManager *cm = ConfigManager :: getConfigManager();
    ConfigStore *s;
    IndexedList<ConfigStore *> *stores = cm->getStores();
    int path_elements = args.get_path_depth();

    if (path_elements > 1) {
        resp->error("Path depth exceeds 1.");
        resp->json_response(HTTP_BAD_REQUEST);
        return;
    }
    JSON_List *list = NULL;
    if (path_elements == 1) {
        list = JSON::List();
        resp->json->add("reset", list);
    }

    for(int n = 0; n < (*stores).get_elements();n++) {
        s = (*stores)[n];
        if ((path_elements == 0) || (pattern_match(args.get_path(0), s->get_store_name()))) {
            s->reset();
            s->effectuate();
            if (path_elements == 1) {
                list->add(s->get_store_name());
            }
        }
    }
    resp->json_response(HTTP_OK);
}
