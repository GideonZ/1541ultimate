#include <iostream>
#include <fstream>
#include <string>
#include <gtest/gtest.h>
#include <stdio.h>

#define ENTER_SAFE_SECTION
#define LEAVE_SAFE_SECTION

#include "json.h"

extern "C" {
    void outbyte(int c) {
        std::cout << (char)c;
    }
}

class JSONDecodeTest : public ::testing::Test {
protected:
    // SetUp and TearDown executes for each test case.
    void SetUp() override {
    }

    void TearDown() override {
    }

    // Class members are accessible from test cases. Reinitiated before each test.
    char raw_space[16384];
    size_t used_bytes;

    void read_file(const char *fn)
    {
        std::ifstream InFile(fn);
        InFile.read(raw_space, 16384);
        std::streamsize bytes = InFile.gcount();
        raw_space[bytes] = 0;
        InFile.close();
        used_bytes = bytes;
    }    

};

//////////////////////////////////////////////////////////
//                  JSON TESTS                          //
//////////////////////////////////////////////////////////
TEST_F(JSONDecodeTest, ReadConfig)
{
    read_file("cfg.json");

    JSON *obj = NULL;
    JSON *obj2 = NULL;

    int tokens = convert_text_to_json_objects(raw_space, used_bytes, 256, &obj);

    EXPECT_EQ(tokens, 97);
    EXPECT_NE((long)obj, 0);
    
    if (obj) {
        char *rendered = (char *)obj->render();

        // now try to re-parse the rendered json
        int tokens2 = convert_text_to_json_objects((char *)rendered, strlen(rendered), 256, &obj2);

        EXPECT_EQ(tokens2, 97);
        EXPECT_NE((long)obj2, 0);
    }

    if (obj) {
        delete obj;
    }
    if (obj2) {
        delete obj2;
    }
}
