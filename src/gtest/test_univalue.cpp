// Copyright 2014 BitPay, Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <vector>
#include <string>
#include <map>
#include <univalue.h>
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class univalue_tests : public BitcoinBasicTestingSetup {};

TEST_F(univalue_tests, univalue_constructor)
{
    UniValue v1;
    EXPECT_TRUE(v1.isNull());

    UniValue v2(UniValue::VSTR);
    EXPECT_TRUE(v2.isStr());

    UniValue v3(UniValue::VSTR, "foo");
    EXPECT_TRUE(v3.isStr());
    EXPECT_EQ(v3.getValStr(), "foo");

    UniValue numTest;
    EXPECT_TRUE(numTest.setNumStr("82"));
    EXPECT_TRUE(numTest.isNum());
    EXPECT_EQ(numTest.getValStr(), "82");

    uint64_t vu64 = 82;
    UniValue v4(vu64);
    EXPECT_TRUE(v4.isNum());
    EXPECT_EQ(v4.getValStr(), "82");

    int64_t vi64 = -82;
    UniValue v5(vi64);
    EXPECT_TRUE(v5.isNum());
    EXPECT_EQ(v5.getValStr(), "-82");

    int vi = -688;
    UniValue v6(vi);
    EXPECT_TRUE(v6.isNum());
    EXPECT_EQ(v6.getValStr(), "-688");

    double vd = -7.21;
    UniValue v7(vd);
    EXPECT_TRUE(v7.isNum());
    EXPECT_EQ(v7.getValStr(), "-7.21");

    std::string vs("yawn");
    UniValue v8(vs);
    EXPECT_TRUE(v8.isStr());
    EXPECT_EQ(v8.getValStr(), "yawn");

    const char *vcs = "zappa";
    UniValue v9(vcs);
    EXPECT_TRUE(v9.isStr());
    EXPECT_EQ(v9.getValStr(), "zappa");
}

TEST_F(univalue_tests, univalue_typecheck)
{
    UniValue v1;
    EXPECT_TRUE(v1.setNumStr("1"));
    EXPECT_TRUE(v1.isNum());
    EXPECT_THROW(v1.get_bool(), std::runtime_error);

    UniValue v2;
    EXPECT_TRUE(v2.setBool(true));
    EXPECT_EQ(v2.get_bool(), true);
    EXPECT_THROW(v2.get_int(), std::runtime_error);

    UniValue v3;
    EXPECT_TRUE(v3.setNumStr("32482348723847471234"));
    EXPECT_THROW(v3.get_int64(), std::runtime_error);
    EXPECT_TRUE(v3.setNumStr("1000"));
    EXPECT_EQ(v3.get_int64(), 1000);

    UniValue v4;
    EXPECT_TRUE(v4.setNumStr("2147483648"));
    EXPECT_EQ(v4.get_int64(), 2147483648);
    EXPECT_THROW(v4.get_int(), std::runtime_error);
    EXPECT_TRUE(v4.setNumStr("1000"));
    EXPECT_EQ(v4.get_int(), 1000);
    EXPECT_THROW(v4.get_str(), std::runtime_error);
    EXPECT_EQ(v4.get_real(), 1000);
    EXPECT_THROW(v4.get_array(), std::runtime_error);
    EXPECT_THROW(v4.getKeys(), std::runtime_error);
    EXPECT_THROW(v4.getValues(), std::runtime_error);
    EXPECT_THROW(v4.get_obj(), std::runtime_error);

    UniValue v5;
    EXPECT_TRUE(v5.read("[true, 10]"));
    EXPECT_NO_THROW(v5.get_array());
    std::vector<UniValue> vals = v5.getValues();
    EXPECT_THROW(vals[0].get_int(), std::runtime_error);
    EXPECT_EQ(vals[0].get_bool(), true);

    EXPECT_EQ(vals[1].get_int(), 10);
    EXPECT_THROW(vals[1].get_bool(), std::runtime_error);
}

TEST_F(univalue_tests, univalue_set)
{
    UniValue v(UniValue::VSTR, "foo");
    v.clear();
    EXPECT_TRUE(v.isNull());
    EXPECT_EQ(v.getValStr(), "");

    EXPECT_TRUE(v.setObject());
    EXPECT_TRUE(v.isObject());
    EXPECT_EQ(v.size(), 0u);
    EXPECT_EQ(v.getType(), UniValue::VOBJ);
    EXPECT_TRUE(v.empty());

    EXPECT_TRUE(v.setArray());
    EXPECT_TRUE(v.isArray());
    EXPECT_EQ(v.size(), 0u);

    EXPECT_TRUE(v.setStr("zum"));
    EXPECT_TRUE(v.isStr());
    EXPECT_EQ(v.getValStr(), "zum");

    EXPECT_TRUE(v.setFloat(-1.01));
    EXPECT_TRUE(v.isNum());
    EXPECT_EQ(v.getValStr(), "-1.01");

    EXPECT_TRUE(v.setInt((int)1023));
    EXPECT_TRUE(v.isNum());
    EXPECT_EQ(v.getValStr(), "1023");

    EXPECT_TRUE(v.setInt((int64_t)-1023LL));
    EXPECT_TRUE(v.isNum());
    EXPECT_EQ(v.getValStr(), "-1023");

    EXPECT_TRUE(v.setInt((uint64_t)1023ULL));
    EXPECT_TRUE(v.isNum());
    EXPECT_EQ(v.getValStr(), "1023");

    EXPECT_TRUE(v.setNumStr("-688"));
    EXPECT_TRUE(v.isNum());
    EXPECT_EQ(v.getValStr(), "-688");

    EXPECT_TRUE(v.setBool(false));
    EXPECT_EQ(v.isBool(), true);
    EXPECT_EQ(v.isTrue(), false);
    EXPECT_EQ(v.isFalse(), true);
    EXPECT_EQ(v.getBool(), false);

    EXPECT_TRUE(v.setBool(true));
    EXPECT_EQ(v.isBool(), true);
    EXPECT_EQ(v.isTrue(), true);
    EXPECT_EQ(v.isFalse(), false);
    EXPECT_EQ(v.getBool(), true);

    EXPECT_TRUE(!v.setNumStr("zombocom"));

    EXPECT_TRUE(v.setNull());
    EXPECT_TRUE(v.isNull());
}

TEST_F(univalue_tests, univalue_array)
{
    UniValue arr(UniValue::VARR);

    UniValue v((int64_t)1023LL);
    EXPECT_TRUE(arr.push_back(v));

    std::string vStr("zippy");
    EXPECT_TRUE(arr.push_back(vStr));

    const char *s = "pippy";
    EXPECT_TRUE(arr.push_back(s));

    std::vector<UniValue> vec;
    v.setStr("boing");
    vec.push_back(v);

    v.setStr("going");
    vec.push_back(v);

    EXPECT_TRUE(arr.push_backV(vec));

    EXPECT_EQ(arr.empty(), false);
    EXPECT_EQ(arr.size(), 5u);

    EXPECT_EQ(arr[0].getValStr(), "1023");
    EXPECT_EQ(arr[1].getValStr(), "zippy");
    EXPECT_EQ(arr[2].getValStr(), "pippy");
    EXPECT_EQ(arr[3].getValStr(), "boing");
    EXPECT_EQ(arr[4].getValStr(), "going");

    EXPECT_EQ(arr[999].getValStr(), "");

    arr.clear();
    EXPECT_TRUE(arr.empty());
    EXPECT_EQ(arr.size(), 0u);
}

TEST_F(univalue_tests, univalue_object)
{
    UniValue obj(UniValue::VOBJ);
    std::string strKey, strVal;
    UniValue v;

    strKey = "age";
    v.setInt(100);
    EXPECT_TRUE(obj.pushKV(strKey, v));

    strKey = "first";
    strVal = "John";
    EXPECT_TRUE(obj.pushKV(strKey, strVal));

    strKey = "last";
    const char *cVal = "Smith";
    EXPECT_TRUE(obj.pushKV(strKey, cVal));

    strKey = "distance";
    EXPECT_TRUE(obj.pushKV(strKey, (int64_t) 25));

    strKey = "time";
    EXPECT_TRUE(obj.pushKV(strKey, (uint64_t) 3600));

    strKey = "calories";
    EXPECT_TRUE(obj.pushKV(strKey, (int) 12));

    strKey = "temperature";
    EXPECT_TRUE(obj.pushKV(strKey, (double) 90.012));

    UniValue obj2(UniValue::VOBJ);
    EXPECT_TRUE(obj2.pushKV("cat1", 9000));
    EXPECT_TRUE(obj2.pushKV("cat2", 12345));

    EXPECT_TRUE(obj.pushKVs(obj2));

    EXPECT_EQ(obj.empty(), false);
    EXPECT_EQ(obj.size(), 9u);

    EXPECT_EQ(obj["age"].getValStr(), "100");
    EXPECT_EQ(obj["first"].getValStr(), "John");
    EXPECT_EQ(obj["last"].getValStr(), "Smith");
    EXPECT_EQ(obj["distance"].getValStr(), "25");
    EXPECT_EQ(obj["time"].getValStr(), "3600");
    EXPECT_EQ(obj["calories"].getValStr(), "12");
    EXPECT_EQ(obj["temperature"].getValStr(), "90.012");
    EXPECT_EQ(obj["cat1"].getValStr(), "9000");
    EXPECT_EQ(obj["cat2"].getValStr(), "12345");

    EXPECT_EQ(obj["nyuknyuknyuk"].getValStr(), "");

    EXPECT_TRUE(obj.exists("age"));
    EXPECT_TRUE(obj.exists("first"));
    EXPECT_TRUE(obj.exists("last"));
    EXPECT_TRUE(obj.exists("distance"));
    EXPECT_TRUE(obj.exists("time"));
    EXPECT_TRUE(obj.exists("calories"));
    EXPECT_TRUE(obj.exists("temperature"));
    EXPECT_TRUE(obj.exists("cat1"));
    EXPECT_TRUE(obj.exists("cat2"));

    EXPECT_TRUE(!obj.exists("nyuknyuknyuk"));

    std::map<std::string, UniValue::VType> objTypes;
    objTypes["age"] = UniValue::VNUM;
    objTypes["first"] = UniValue::VSTR;
    objTypes["last"] = UniValue::VSTR;
    objTypes["distance"] = UniValue::VNUM;
    objTypes["time"] = UniValue::VNUM;
    objTypes["calories"] = UniValue::VNUM;
    objTypes["temperature"] = UniValue::VNUM;
    objTypes["cat1"] = UniValue::VNUM;
    objTypes["cat2"] = UniValue::VNUM;
    EXPECT_TRUE(obj.checkObject(objTypes));

    objTypes["cat2"] = UniValue::VSTR;
    EXPECT_TRUE(!obj.checkObject(objTypes));

    obj.clear();
    EXPECT_TRUE(obj.empty());
    EXPECT_EQ(obj.size(), 0u);
}

static const char *json1 =
"[1.10000000,{\"key1\":\"str\\u0000\",\"key2\":800,\"key3\":{\"name\":\"martian http://test.com\"}}]";

TEST_F(univalue_tests, univalue_readwrite)
{
    UniValue v;
    EXPECT_TRUE(v.read(json1));

    std::string strJson1(json1);
    EXPECT_TRUE(v.read(strJson1));

    EXPECT_TRUE(v.isArray());
    EXPECT_EQ(v.size(), 2u);

    EXPECT_EQ(v[0].getValStr(), "1.10000000");

    UniValue obj = v[1];
    EXPECT_TRUE(obj.isObject());
    EXPECT_EQ(obj.size(), 3u);

    EXPECT_TRUE(obj["key1"].isStr());
    std::string correctValue("str");
    correctValue.push_back('\0');
    EXPECT_EQ(obj["key1"].getValStr(), correctValue);
    EXPECT_TRUE(obj["key2"].isNum());
    EXPECT_EQ(obj["key2"].getValStr(), "800");
    EXPECT_TRUE(obj["key3"].isObject());

    EXPECT_EQ(strJson1, v.write());
}
