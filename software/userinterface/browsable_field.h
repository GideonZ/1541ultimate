#ifndef BROWSABLE_FIELD
#define BROWSABLE_FIELD
#include "browsable.h"
#include "json.h"
#include "subsys.h"

class BrowsableQueryField: public Browsable
{
    const char *field;
    JSON *target; // When this points to a list, use a drop down menu
    mstring value; // Visible value in the form
    int current_preset; // to know where we are at in the list of presets

    static SubsysResultCode_e update_preset(SubsysCommand *cmd)
    {
        BrowsableQueryField *field = (BrowsableQueryField *)cmd->functionID;
        field->current_preset = cmd->mode;
        field->setPreset();
        return SSRET_OK;
    }
public:
    BrowsableQueryField(const char *field, JSON *target) : field(field), target(target)
    {
        current_preset = -1; // none
        if (target && target->type() != eList) {
            value = target->render_compact();
        }
    }

    ~BrowsableQueryField()
    {
    }

    const char *getName()
    {
        return field;
    }

    void reset()
    {
        current_preset = -1;
        value = "";
    }

    const char *getAqlString()
    {
        if (target && target->type() == eList) {
            JSON_List *presets = (JSON_List *)target;
            if (current_preset == -1) {
                return "";
            }
            JSON_Object *obj = (JSON_Object *)(*presets)[current_preset];
            JSON_String *aql = (JSON_String *)obj->get("aqlKey");
            if (aql) {
                return aql->get_string();
            }
        }
        return value.c_str();
    }

    const char *getStringValue()
    {
        return value.c_str();
    }

    void setStringValue(const char *s)
    {
        value = s;
        if (!target)
            return;
        if (target->type() == eString) {
            ((JSON_String *)target)->set_string(s);
        } else if (target->type() == eInteger) {
            int val = 0;
            int ret = sscanf(s, "%d", &val);
            if (ret) {
                char temp[12];
                sprintf(temp, "%d", val);
                value = temp;
                ((JSON_Integer *)target)->set_value(val);
            }
        }
    }

    bool isDropDown(void)
    {
        return (target && target->type() == eList);
    }

    void updown(int offset)
    {
        if (!target) {
            return;
        }
        if (target->type() == eList) { // preset
            JSON_List *presets = (JSON_List *)target;

            current_preset += offset;
            if (current_preset < 0)
                current_preset = 0;
            if (current_preset >= presets->get_num_elements()) {
                current_preset = presets->get_num_elements() - 1;
            }
            setPreset();
        } else if(target->type() == eInteger) {
            JSON_Integer *val = (JSON_Integer *)target;
            val->set_value(val->get_value() + offset);
            // Update Display string
            char temp[12];
            sprintf(temp, "%d", val->get_value());
            value = temp;
        }
    }

    void setPreset()
    {
        if (!target) {
            return;
        }
        if (target->type() == eList) { // preset
            JSON_List *presets = (JSON_List *)target;
            JSON *el = (*presets)[current_preset];
            if (el->type() != eObject) {
                return;
            }
            JSON_Object *obj = (JSON_Object *)el;
            JSON *value = obj->get("name");
            if (!value) {
                value = obj->get("aqlKey");
            }
            if (value->type() != eString) {
                return;
            }
            setStringValue(((JSON_String *)value)->get_string());
        }
    }

    void getDisplayString(char *buffer, int width)
    {
        // minimum window size = 38
        // Field name: 8 chars max, colon, space = 10. Left: 28
        // Max field length = 26

        memset(buffer, ' ', width+2);
        buffer[width+2] = '\0';

        if (width < 16) {
            return;
        }

        if (field[0] == '$') {
            sprintf(buffer, "\er            <<  Submit  >> ");
            return;
        }

        sprintf(buffer, "%s:", field);
        buffer[strlen(buffer)] = ' ';
        if (width < 16) {
            return;
        }

        if (value.length()) {
            // position 10
            sprintf(buffer+10, "\er\eg%#s", width-11, value.c_str());
        } else {
            sprintf(buffer+10, "\er\ek%#s", width-11, "__________________");
        }
        if (buffer[0] > 0x60)
            buffer[0] &= 0xDF; // Capitalize ;-)
    }

    void fetch_context_items(IndexedList<Action *>&actions)
    {
        if (!target || target->type() != eList) { // preset
            return;
        }

        JSON_List *presets = (JSON_List *)target;
        for (int i = 0; i < presets->get_num_elements(); i++) {
            JSON *el = (*presets)[i];
            if (el->type() != eObject) {
                continue;
            }
            JSON_Object *obj = (JSON_Object *)el;
            JSON *value = obj->get("name");
            if (!value) {
                value = obj->get("aqlKey");
            }
            if (value->type() != eString) {
                continue;
            }
            actions.append(new Action(((JSON_String *)value)->get_string(), update_preset, (int)this, i));          
        }
    }

};

#endif
