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
                } else if(i->definition->type == CFG_TYPE_STRFUNC) {
                    JSON_List *list = JSON::List();
                    ob->add("presets", list);
                    t_cfg_strfunc preset_func = (t_cfg_strfunc)i->definition->items;
                    IndexedList<char *> presets(8, NULL);
                    (preset_func)(NULL, presets);
                    for(int j=0;j < presets.get_elements(); j++) {
                        char *preset = presets[j];
                        list->add(preset);
                        delete [] preset;
                    }
                    ob->add("default", (const char *)i->definition->def);
                } else if((i->definition->type == CFG_TYPE_STRING) || (i->definition->type == CFG_TYPE_STRPASS)) {
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

API_DOC(GET, configs, none,
    TAG("Configuration")
    SUMMARY("Read configuration")
    DESCRIPTION("Reads the same settings the menu shows, at one of three levels of detail. With "
                "no path it lists the category names. With a category it gives the current value "
                "of every item in it. With a category and an item it describes each matching item "
                "in full: the current value, the default, and either the accepted values of an "
                "enumeration or the range and format of a number.\n"
                "\n"
                "Both path elements are patterns, so `drive*` selects every category whose name "
                "starts with `drive` and `*bus*` selects every item with `bus` in its name. "
                "Category and item names contain spaces and must be URL encoded.\n"
                "\n"
                "Which categories exist depends on the product and on what is installed, so the "
                "list is not the same on two devices.")
    PATH("/v1/configs", "listConfigCategories", "List the configuration categories")
    PATH("/v1/configs/{category}", "getConfigCategory", "Read the values in a category")
    PATH("/v1/configs/{category}/{item}", "getConfigItem", "Read a setting in full")
    PATH_PARAM("category", "string", "Category name or pattern. Names contain spaces, so this has to be URL encoded.", "Drive%20A%20Settings")
    PATH_PARAM("item", "string", "Item name or pattern, URL encoded.", "Drive%20Bus%20ID")
    RESPONSE("200", "application/json", "ConfigCategoriesResponse", "The categories on this device.", "listConfigCategories")
    RESPONSE_EXAMPLE("200", "Categories", "{\n  \"categories\" : [ \"Drive A Settings\", \"Network Settings\", \"User Interface Settings\" ],\n  \"errors\" : []\n}", "listConfigCategories")
    RESPONSE("200", "application/json", "ConfigValuesResponse", "The current value of every matching item.", "getConfigCategory")
    RESPONSE_EXAMPLE("200", "Drive A", "{\n  \"Drive A Settings\" : {\n    \"Drive\" : \"Enabled\",\n    \"Drive Type\" : \"1541\",\n    \"Drive Bus ID\" : 8\n  },\n  \"errors\" : []\n}", "getConfigCategory")
    RESPONSE("200", "application/json", "ConfigItemsResponse", "Every matching item, described in full.", "getConfigItem")
    RESPONSE_EXAMPLE("200", "One item", "{\n  \"Drive A Settings\" : {\n    \"Drive Bus ID\" : {\n      \"current\" : 8,\n      \"min\" : 8,\n      \"max\" : 11,\n      \"format\" : \"%d\",\n      \"default\" : 8\n    }\n  },\n  \"errors\" : []\n}", "getConfigItem")
    RESPONSE_ERROR("404", "No configuration category matches 'drive c*'.", "getConfigCategory")
    RESPONSE_ERROR("404", "No configuration category matches 'drive c*'.", "getConfigItem")
)
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
        bool matched = false;
        for(int i=0; i<stores->get_elements(); i++) {
            ConfigStore *s = (*stores)[i];
            if ((path_elements < 1) || pattern_match(args.get_path(0), s->get_store_name())) {
                emit_store(s, resp->json, args);
                matched = true;
            }
        }
        if (!matched) {
            // A name no store answers to used to come back as 200 with an empty
            // body, which reads as "the category exists and holds nothing"
            // rather than as the typo it is.
            resp->error("No configuration category matches '%s'.", args.get_path(0));
            resp->json_response(HTTP_NOT_FOUND);
            return;
        }
    }

    resp->json_response(HTTP_OK);
}

API_DOC(PUT, configs, none,
    TAG("Configuration")
    SUMMARY("Change a setting")
    DESCRIPTION("Sets one setting. The path names the category and the item, both matched as "
                "patterns, and the first match wins, so a pattern that matches more than one item "
                "sets whichever comes first.\n"
                "\n"
                "The new value is given in one of two ways, and in exactly one of them: as the "
                "`value` query argument on a two element path, or as a third path element with no "
                "query argument. Any other shape is answered with 400.\n"
                "\n"
                "The change takes effect at once but lives only in memory. It survives until the "
                "device reboots, and no longer, unless `configs:save_to_flash` writes it.")
    PATH("/v1/configs/{category}/{item}", "setConfigItem", "Set a value passed as a query argument")
    PATH("/v1/configs/{category}/{item}/{value}", "setConfigItemByPath", "Set a value passed as a path element")
    PATH_PARAM("category", "string", "Category name or pattern. Names contain spaces, so this has to be URL encoded.", "Drive%20A%20Settings")
    PATH_PARAM("item", "string", "Item name or pattern, URL encoded.", "Drive%20Bus%20ID")
    PATH_PARAM("value", "string", "The new value, URL encoded. Three element form only; the query argument has to be absent.", "9")
    PARAM("value", "string", "The new value: one of the accepted values for an enumeration, a number for a numeric setting, or free text for a string setting. Two element form only.", "", "9")
    RESPONSE("200", "application/json", "ErrorResponse", "The setting was changed.", "")
    RESPONSE_ERROR("400", "When using the 'value' argument, the path should have two elements; the config category and the config item name. Given: '/Drive A Settings'", "")
    RESPONSE_ERROR("400", "Value 12 is outside of the allowable range (8-11)", "")
    RESPONSE_ERROR("400", "Value 'Maybe' is not a valid choice for item Drive", "")
    RESPONSE_ERROR("404", "There is no config category that matches 'drive c*'", "")
    RESPONSE_ERROR("404", "Item 'speed' not found in this configuration category [Drive A Settings].", "")
)
// 'value' is optional because this route takes it either as the query argument
// or as a third path element, and the handler below chooses between them and
// rejects every other shape. Declaring it required had the validator refuse the
// path form before the handler ran, so the branch that reads it was dead.
API_CALL(PUT, configs, none, NULL, ARRAY ( { {"value", P_OPTIONAL }} ))
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

API_DOC(POST, configs, none,
    TAG("Configuration")
    SUMMARY("Change several settings at once")
    DESCRIPTION("Applies a JSON object of categories to items to values in a single request, "
                "which is how a full configuration is restored without one call per setting.\n"
                "\n"
                "Names are matched exactly here, not as patterns. Every item in the body is "
                "attempted whatever happens to the others, so a body with one bad name still "
                "applies the rest and the response says which ones failed. Like the PUT form, "
                "this changes memory only until `configs:save_to_flash` is called.\n"
                "\n"
                "The request must be `application/json` and at most 32 KB.")
    PATH("/v1/configs", "updateConfigs", "")
    BODY("application/json", "ConfigUpdate", "Categories, items and the values to set.")
    RESPONSE("200", "application/json", "ErrorResponse", "Every setting in the body was applied.", "")
    RESPONSE("400", "application/json", "ErrorResponse", "Nothing could be parsed, or some settings were rejected. The errors array names them.", "")
    RESPONSE_EXAMPLE("400", "One bad item name", "{\n  \"errors\" : [ \"'Drive Speed' is not a valid item name in category 'Drive A Settings'.\" ]\n}", "")
    RESPONSE_ERROR("500", "Could not buffer attachment.", "")
)
API_CALL(POST, configs, none, &attachment_writer, ARRAY ( { } ))
{
    if (!req->ContentType || strcasecmp(req->ContentType, "application/json") != 0)  {
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


API_DOC(PUT, configs, load_from_flash,
    TAG("Configuration")
    SUMMARY("Reload settings from flash")
    CAUTION("destructive", "Settings changed since the last save are discarded.")
    DESCRIPTION("Throws away the settings in memory and reads them back from flash, undoing every "
                "change that has not been saved. With a category in the path only that category "
                "is reloaded and the response lists what was touched.")
    PATH("/v1/configs:load_from_flash", "loadConfigsFromFlash", "Reload every category")
    PATH("/v1/configs/{category}:load_from_flash", "loadConfigCategoryFromFlash", "Reload matching categories")
    PATH_PARAM("category", "string", "Category name or pattern, URL encoded.", "Drive%20A%20Settings")
    RESPONSE("200", "application/json", "ErrorResponse", "Every category was reloaded.", "loadConfigsFromFlash")
    RESPONSE("200", "application/json", "ConfigStoreListResponse", "The categories that were reloaded.", "loadConfigCategoryFromFlash")
    RESPONSE_ERROR("400", "Path depth exceeds 1.", "")
)
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

API_DOC(PUT, configs, save_to_flash,
    TAG("Configuration")
    SUMMARY("Save settings to flash")
    CAUTION("persistent", "Writes flash. A device that booted into safe mode holds defaults, and saving then replaces the stored values with them.")
    DESCRIPTION("Writes the settings that have changed since the last save into flash, so that "
                "they survive a power cycle. Only categories that are actually stale are written, "
                "and the response lists which ones those were, so an empty list means there was "
                "nothing to do.\n"
                "\n"
                "Take care when the device has booted into safe mode: the settings in memory are "
                "defaults then, and saving replaces the stored values with them.")
    PATH("/v1/configs:save_to_flash", "saveConfigsToFlash", "Save every stale category")
    PATH("/v1/configs/{category}:save_to_flash", "saveConfigCategoryToFlash", "Save matching categories")
    PATH_PARAM("category", "string", "Category name or pattern, URL encoded.", "Drive%20A%20Settings")
    RESPONSE("200", "application/json", "ConfigStoreListResponse", "The categories that were written.", "")
    RESPONSE_EXAMPLE("200", "One category written", "{\n  \"written\" : [ \"Drive A Settings\" ],\n  \"errors\" : []\n}", "")
    RESPONSE_ERROR("400", "Path depth exceeds 1.", "")
)
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

API_DOC(PUT, configs, reset_to_default,
    TAG("Configuration")
    SUMMARY("Reset settings to their defaults")
    CAUTION("destructive", "Every setting the machine was configured with is replaced by its factory value.")
    DESCRIPTION("Puts the settings back to their factory values and applies them immediately. "
                "Flash is not touched, so `configs:load_from_flash` undoes this and "
                "`configs:save_to_flash` makes it permanent.")
    PATH("/v1/configs:reset_to_default", "resetConfigsToDefault", "Reset every category")
    PATH("/v1/configs/{category}:reset_to_default", "resetConfigCategoryToDefault", "Reset matching categories")
    PATH_PARAM("category", "string", "Category name or pattern, URL encoded.", "Drive%20A%20Settings")
    RESPONSE("200", "application/json", "ErrorResponse", "Every category was reset.", "resetConfigsToDefault")
    RESPONSE("200", "application/json", "ConfigStoreListResponse", "The categories that were reset.", "resetConfigCategoryToDefault")
    RESPONSE_ERROR("400", "Path depth exceeds 1.", "")
)
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
