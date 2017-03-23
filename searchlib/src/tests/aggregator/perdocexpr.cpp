// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include <vespa/searchlib/aggregation/aggregation.h>
#include <vespa/searchlib/aggregation/expressioncountaggregationresult.h>
#include <vespa/searchlib/aggregation/perdocexpression.h>
#include <vespa/searchlib/attribute/extendableattributes.h>
#include <vespa/vespalib/objects/objectdumper.h>
#include <vespa/vespalib/testkit/testapp.h>
#include <stdexcept>
#include <vespa/document/base/testdocman.h>
#include <vespa/vespalib/util/md5.h>
#include <vespa/vespalib/util/stringfmt.h>
#include <vespa/searchlib/expression/getdocidnamespacespecificfunctionnode.h>
#include <vespa/searchlib/expression/documentfieldnode.h>
#include <cmath>
#include <iostream>

#define MU std::make_unique

using namespace search;
using namespace search::expression;
using namespace search::aggregation;
using namespace vespalib;

struct AggrGetter {
    virtual ~AggrGetter() { }
    virtual const ResultNode &operator()(const AggregationResult &r) const = 0;
};

AttributeGuard createInt64Attribute();
AttributeGuard createInt32Attribute();
AttributeGuard createInt16Attribute();
AttributeGuard createInt8Attribute();
template<typename T>
void testCmp(const T & small, const T & medium, const T & large);

void testMin(const ResultNode & a, const ResultNode & b) {
    ASSERT_TRUE(a.cmp(b) < 0);
    MinFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
        .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false).execute();
    ASSERT_TRUE(func.getResult().cmp(a) == 0);

    MinFunctionNode funcR;
    funcR.appendArg(MU<ConstantNode>(ResultNode::UP(b.clone())))
         .appendArg(MU<ConstantNode>(ResultNode::UP(a.clone()))).prepare(false).execute();
    ASSERT_TRUE(funcR.getResult().cmp(a) == 0);
}

TEST("testMin") {
    testMin(Int64ResultNode(67), Int64ResultNode(68));
    testMin(FloatResultNode(67), FloatResultNode(68));
    testMin(StringResultNode("67"), StringResultNode("68"));
    testMin(RawResultNode("67", 2), RawResultNode("68", 2));
    testMin(RawResultNode("-67", 2), RawResultNode("68", 2));
}

void testMax(const ResultNode & a, const ResultNode & b) {
    ASSERT_TRUE(a.cmp(b) < 0);
    MaxFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
        .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false)
        .execute();
    ASSERT_TRUE(func.getResult().cmp(b) == 0);

    MaxFunctionNode funcR;
    funcR.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
         .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false)
         .execute();
    ASSERT_TRUE(funcR.getResult().cmp(b) == 0);
}

TEST("testMax") {
    testMax(Int64ResultNode(67), Int64ResultNode(68));
    testMax(FloatResultNode(67), FloatResultNode(68));
    testMax(StringResultNode("67"), StringResultNode("68"));
    testMax(RawResultNode("67", 2), RawResultNode("68", 2));
    testMax(RawResultNode("-67", 2), RawResultNode("68", 2));
}

ExpressionCountAggregationResult getExpressionCountWithNormalSketch() {
    nbostream stream;
    stream << (uint32_t)ExpressionCountAggregationResult::classId
           << (char)0 << (uint32_t)0
           << (uint32_t)NormalSketch<>::classId
           << NormalSketch<>::BUCKET_COUNT << NormalSketch<>::BUCKET_COUNT;
    for (size_t i = 0; i < NormalSketch<>::BUCKET_COUNT; ++i) {
        stream << static_cast<char>(0);
    }
    NBOSerializer serializer(stream);
    ExpressionCountAggregationResult result;
    serializer >> result;
    EXPECT_EQUAL(0u, stream.size());
    EXPECT_EQUAL(NormalSketch<>(), result.getSketch());
    return result;
}

void testExpressionCount(const ResultNode &a, uint32_t bucket, uint8_t val) {
    ExpressionCountAggregationResult func = getExpressionCountWithNormalSketch();
    func.setExpression(MU<ConstantNode>(ResultNode::UP(a.clone())));
    func.aggregate(DocId(42), HitRank(21));

    const auto &sketch = func.getSketch();
    auto normal = dynamic_cast<const NormalSketch<>&>(sketch);
    for (uint32_t i = 0; i < sketch.BUCKET_COUNT; ++i) {
        TEST_STATE(make_string("Bucket %u. Expected bucket %u=%u", i, bucket, val).c_str());
        EXPECT_EQUAL(i == bucket? val : 0, (int) normal.bucket[i]);
    }
}

TEST("require that expression count can operate on different results") {
    testExpressionCount(Int64ResultNode(67), 98, 2);
    testExpressionCount(FloatResultNode(67), 545, 1);
    testExpressionCount(StringResultNode("67"), 243, 1);
    testExpressionCount(RawResultNode("67", 2), 243, 1);
    testExpressionCount(RawResultNode("-67", 2), 434, 1);
}

TEST("require that expression counts can be merged") {
    ExpressionCountAggregationResult func1 = getExpressionCountWithNormalSketch();
    func1.setExpression(MU<ConstantNode>(MU<Int64ResultNode>(67))).aggregate(DocId(42), HitRank(21));
    ExpressionCountAggregationResult func2 = getExpressionCountWithNormalSketch();
    func2.setExpression(MU<ConstantNode>(MU<FloatResultNode>(67))).aggregate(DocId(42), HitRank(21));

    EXPECT_EQUAL(2, func1.getRank().getInteger());
    func1.merge(func2);
    EXPECT_EQUAL(3, func1.getRank().getInteger());
    const auto &sketch = func1.getSketch();
    auto normal = dynamic_cast<const NormalSketch<>&>(sketch);
    EXPECT_EQUAL(2, normal.bucket[98]);  // from func1
    EXPECT_EQUAL(1, normal.bucket[545]);  // from func2
}

TEST("require that expression counts can be serialized") {
    ExpressionCountAggregationResult func;
    func.setExpression(MU<ConstantNode>(MU<Int64ResultNode>(67))).aggregate(DocId(42), HitRank(21));
    func.setExpression(MU<ConstantNode>(MU<Int64ResultNode>(68))).aggregate(DocId(42), HitRank(21));

    nbostream os;
    NBOSerializer nos(os);
    nos << func;
    Identifiable::UP obj = Identifiable::create(nos);
    auto *func2 = dynamic_cast<ExpressionCountAggregationResult *>(obj.get());
    ASSERT_TRUE(func2);
    EXPECT_EQUAL(func.getSketch(), func2->getSketch());
}

TEST("require that expression count estimates rank") {
    ExpressionCountAggregationResult func = getExpressionCountWithNormalSketch();
    EXPECT_EQUAL(0, func.getRank().getInteger());
    func.setExpression(MU<ConstantNode>(MU<Int64ResultNode>(67))).aggregate(DocId(42), HitRank(21));
    EXPECT_EQUAL(2, func.getRank().getInteger());
    func.setExpression(MU<ConstantNode>(MU<FloatResultNode>(67))).aggregate(DocId(42), HitRank(21));
    EXPECT_EQUAL(3, func.getRank().getInteger());
    func.setExpression(MU<ConstantNode>(MU<FloatResultNode>(67))).aggregate(DocId(42), HitRank(21));
    EXPECT_EQUAL(3, func.getRank().getInteger());
}

void testAdd(const ResultNode &a, const ResultNode &b, const ResultNode &c) {
    AddFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
        .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false).execute();
    EXPECT_EQUAL(func.getResult().asString(), c.asString());
    EXPECT_EQUAL(func.getResult().cmp(c), 0);
    EXPECT_EQUAL(c.cmp(func.getResult()), 0);
}

TEST("testAdd") {
    testAdd(Int64ResultNode(67), Int64ResultNode(68), Int64ResultNode(67+68));
    testAdd(FloatResultNode(67), FloatResultNode(68), FloatResultNode(67+68));
    testAdd(StringResultNode("67"), StringResultNode("68"), StringResultNode("lo"));
    testAdd(RawResultNode("67", 2), RawResultNode("68", 2), RawResultNode("lo", 2));
}

void testDivide(const ResultNode &a, const ResultNode &b, const ResultNode &c) {
    DivideFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
        .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false).execute();
    EXPECT_EQUAL(func.getResult().asString(), c.asString());
    EXPECT_EQUAL(func.getResult().getFloat(), c.getFloat());
    EXPECT_EQUAL(func.getResult().cmp(c), 0);
    EXPECT_EQUAL(c.cmp(func.getResult()), 0);
}

TEST("testDivide") {
    testDivide(Int64ResultNode(6), FloatResultNode(12.0), FloatResultNode(0.5));
    testDivide(Int64ResultNode(6), Int64ResultNode(1), Int64ResultNode(6));
    testDivide(Int64ResultNode(6), Int64ResultNode(0), Int64ResultNode(0));
}

void testModulo(const ResultNode &a, const ResultNode &b, const ResultNode &c) {
    ModuloFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone())))
        .appendArg(MU<ConstantNode>(ResultNode::UP(b.clone()))).prepare(false).execute();
    EXPECT_EQUAL(func.getResult().asString(), c.asString());
    EXPECT_EQUAL(func.getResult().getFloat(), c.getFloat());
    EXPECT_EQUAL(func.getResult().cmp(c), 0);
    EXPECT_EQUAL(c.cmp(func.getResult()), 0);
}

TEST("testModulo") {
    testModulo(Int64ResultNode(0), Int64ResultNode(6), Int64ResultNode(0));
    testModulo(Int64ResultNode(1), Int64ResultNode(6), Int64ResultNode(1));
    testModulo(Int64ResultNode(2), Int64ResultNode(6), Int64ResultNode(2));
    testModulo(Int64ResultNode(3), Int64ResultNode(6), Int64ResultNode(3));
    testModulo(Int64ResultNode(4), Int64ResultNode(6), Int64ResultNode(4));
    testModulo(Int64ResultNode(5), Int64ResultNode(6), Int64ResultNode(5));
    testModulo(Int64ResultNode(6), Int64ResultNode(6), Int64ResultNode(0));

    testModulo(Int64ResultNode(6), Int64ResultNode(1), Int64ResultNode(0));
    testModulo(Int64ResultNode(6), Int64ResultNode(0), Int64ResultNode(0));

    testModulo(FloatResultNode(2), Int64ResultNode(6), FloatResultNode(2));
    testModulo(Int64ResultNode(3),   FloatResultNode(6), FloatResultNode(3));
}

void testNegate(const ResultNode & a, const ResultNode & b) {
    NegateFunctionNode func;
    func.appendArg(MU<ConstantNode>(ResultNode::UP(a.clone()))).prepare(false).execute();
    EXPECT_EQUAL(func.getResult().asString(), b.asString());
    EXPECT_EQUAL(func.getResult().cmp(b), 0);
    EXPECT_EQUAL(b.cmp(func.getResult()), 0);
}

TEST("testNegate") {
    testNegate(Int64ResultNode(67), Int64ResultNode(-67));
    testNegate(FloatResultNode(67.0), FloatResultNode(-67.0));

    char strnorm[4] = { 102, 111, 111, 0 };
    char strneg[4] = { -102, -111, -111, 0 };
    testNegate(StringResultNode(strnorm), StringResultNode(strneg));
    testNegate(RawResultNode(strnorm, 3), RawResultNode(strneg, 3));
}

template <typename T>
void testBuckets(const T * b) {
    EXPECT_TRUE(b[0].cmp(b[1]) < 0);
    EXPECT_TRUE(b[1].cmp(b[2]) < 0);
    EXPECT_TRUE(b[2].cmp(b[3]) < 0);
    EXPECT_TRUE(b[3].cmp(b[4]) < 0);
    EXPECT_TRUE(b[4].cmp(b[5]) < 0);

    EXPECT_TRUE(b[1].cmp(b[0]) > 0);
    EXPECT_TRUE(b[2].cmp(b[1]) > 0);
    EXPECT_TRUE(b[3].cmp(b[2]) > 0);
    EXPECT_TRUE(b[4].cmp(b[3]) > 0);
    EXPECT_TRUE(b[5].cmp(b[4]) > 0);

    EXPECT_TRUE(b[1].cmp(b[1]) == 0);
    EXPECT_TRUE(b[2].cmp(b[2]) == 0);
    EXPECT_TRUE(b[3].cmp(b[3]) == 0);
    EXPECT_TRUE(b[4].cmp(b[4]) == 0);
    EXPECT_TRUE(b[5].cmp(b[5]) == 0);

    EXPECT_TRUE(b[0].contains(b[1]) < 0);
    EXPECT_TRUE(b[1].contains(b[2]) < 0);
    EXPECT_TRUE(b[2].contains(b[3]) == 0);
    EXPECT_TRUE(b[3].contains(b[4]) < 0);
    EXPECT_TRUE(b[4].contains(b[5]) < 0);

    EXPECT_TRUE(b[1].contains(b[0]) > 0);
    EXPECT_TRUE(b[2].contains(b[1]) > 0);
    EXPECT_TRUE(b[3].contains(b[2]) == 0);
    EXPECT_TRUE(b[4].contains(b[3]) > 0);
    EXPECT_TRUE(b[5].contains(b[4]) > 0);

    EXPECT_TRUE(b[1].contains(b[1]) == 0);
    EXPECT_TRUE(b[2].contains(b[2]) == 0);
    EXPECT_TRUE(b[3].contains(b[3]) == 0);
    EXPECT_TRUE(b[4].contains(b[4]) == 0);
    EXPECT_TRUE(b[5].contains(b[5]) == 0);
}

TEST("testBuckets") {
    IntegerBucketResultNodeVector iv;
    IntegerBucketResultNodeVector::Vector & ib = iv.getVector();
    EXPECT_TRUE(iv.find(Int64ResultNode(6)) == NULL);
    ib.resize(1);
    ib[0] = IntegerBucketResultNode(7, 9);
    EXPECT_TRUE(iv.find(Int64ResultNode(6)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(7)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(8)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(9)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(10)) == NULL);

    ib.resize(6);
    ib[0] = IntegerBucketResultNode(7, 9);
    ib[1] = IntegerBucketResultNode(13, 17);
    ib[2] = IntegerBucketResultNode(15, 30);
    ib[3] = IntegerBucketResultNode(19, 27);
    ib[4] = IntegerBucketResultNode(20, 33);
    ib[5] = IntegerBucketResultNode(50, 50);
    testBuckets(&ib[0]);
    iv.sort();
    testBuckets(&ib[0]);
    EXPECT_TRUE(ib[0].contains(6) > 0);
    EXPECT_TRUE(ib[0].contains(7) == 0);
    EXPECT_TRUE(ib[0].contains(8) == 0);
    EXPECT_TRUE(ib[0].contains(9) < 0);
    EXPECT_TRUE(ib[0].contains(10) < 0);
    EXPECT_TRUE(iv.find(Int64ResultNode(6)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(7)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(8)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(9)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(10)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(14)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(27)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(32)) != NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(33)) == NULL);
    EXPECT_TRUE(iv.find(Int64ResultNode(50)) == NULL);

    FloatBucketResultNodeVector fv;
    FloatBucketResultNodeVector::Vector & fb = fv.getVector();
    fb.resize(6);
    fb[0] = FloatBucketResultNode(7, 9);
    fb[1] = FloatBucketResultNode(13, 17);
    fb[2] = FloatBucketResultNode(15, 30);
    fb[3] = FloatBucketResultNode(19, 27);
    fb[4] = FloatBucketResultNode(20, 33);
    fb[5] = FloatBucketResultNode(50, 50);
    testBuckets(&fb[0]);
    fv.sort();
    testBuckets(&fb[0]);
    EXPECT_TRUE(fb[0].contains(6) > 0);
    EXPECT_TRUE(fb[0].contains(7) == 0);
    EXPECT_TRUE(fb[0].contains(8) == 0);
    EXPECT_TRUE(fb[0].contains(9) < 0);
    EXPECT_TRUE(fb[0].contains(10) < 0);
    EXPECT_TRUE(fv.find(FloatResultNode(6)) == NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(7)) != NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(8)) != NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(9)) == NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(10)) == NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(14)) != NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(27)) != NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(32)) != NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(33)) == NULL);
    EXPECT_TRUE(fv.find(FloatResultNode(50)) == NULL);

    StringBucketResultNodeVector sv;
    StringBucketResultNodeVector::Vector & sb = sv.getVector();
    sb.resize(6);
    sb[0] = StringBucketResultNode("07", "09");
    sb[1] = StringBucketResultNode("13", "17");
    sb[2] = StringBucketResultNode("15", "30");
    sb[3] = StringBucketResultNode("19", "27");
    sb[4] = StringBucketResultNode("20", "33");
    sb[5] = StringBucketResultNode("50", "50");
    testBuckets(&sb[0]);
    sv.sort();
    testBuckets(&sb[0]);
    EXPECT_TRUE(sb[0].contains("06") > 0);
    EXPECT_TRUE(sb[0].contains("07") == 0);
    EXPECT_TRUE(sb[0].contains("08") == 0);
    EXPECT_TRUE(sb[0].contains("09") < 0);
    EXPECT_TRUE(sb[0].contains("10") < 0);
    EXPECT_TRUE(sv.find(StringResultNode("06")) == NULL);
    EXPECT_TRUE(sv.find(StringResultNode("07")) != NULL);
    EXPECT_TRUE(sv.find(StringResultNode("08")) != NULL);
    EXPECT_TRUE(sv.find(StringResultNode("09")) == NULL);
    EXPECT_TRUE(sv.find(StringResultNode("10")) == NULL);
    EXPECT_TRUE(sv.find(StringResultNode("14")) != NULL);
    EXPECT_TRUE(sv.find(StringResultNode("27")) != NULL);
    EXPECT_TRUE(sv.find(StringResultNode("32")) != NULL);
    EXPECT_TRUE(sv.find(StringResultNode("33")) == NULL);
    EXPECT_TRUE(sv.find(StringResultNode("50")) == NULL);
}

template<typename T>
void testCmp(const T & small, const T & medium, const T & large) {
    EXPECT_TRUE(small.cmp(medium) < 0);
    EXPECT_TRUE(small.cmp(large) < 0);
    EXPECT_TRUE(medium.cmp(large) < 0);
    EXPECT_TRUE(medium.cmp(small) > 0);
    EXPECT_TRUE(large.cmp(small) > 0);
    EXPECT_TRUE(large.cmp(medium) > 0);
}

TEST("testResultNodes") {
    Int64ResultNode i(89);
    char mem[64];
    ResultNode::BufferRef buf(&mem, sizeof(mem));
    EXPECT_EQUAL(i.getInteger(), 89);
    EXPECT_EQUAL(i.getFloat(), 89.0);
    EXPECT_EQUAL(i.getString(buf).c_str(), std::string("89"));
    FloatResultNode f(2165.798);
    EXPECT_EQUAL(f.getInteger(), 2166);
    EXPECT_EQUAL(f.getFloat(), 2165.798);
    EXPECT_EQUAL(f.getString(buf).c_str(), std::string("2165.8"));
    StringResultNode s("17.89hjkljly");
    EXPECT_EQUAL(s.getInteger(), 17);
    EXPECT_EQUAL(s.getFloat(), 17.89);
    EXPECT_EQUAL(s.getString(buf).c_str(), std::string("17.89hjkljly"));
    RawResultNode r("hjgasfdg", 9);
    EXPECT_EQUAL(r.getString(buf).c_str(), std::string("hjgasfdg"));
    int64_t j(789);
    double d(786324.78);
    nbostream os;
    os << j << d;
    RawResultNode r1(os.c_str(), sizeof(j));
    EXPECT_EQUAL(r1.getInteger(), 789);
    RawResultNode r2(os.c_str() + sizeof(j), sizeof(d));
    EXPECT_EQUAL(r2.getFloat(), 786324.78);

    StringResultNode s1, s2("a"), s3("a"), s4("b"), s5("bb");
    EXPECT_EQUAL(s1.cmp(s1), 0);
    EXPECT_EQUAL(s2.cmp(s3), 0);
    EXPECT_EQUAL(s4.cmp(s4), 0);
    EXPECT_EQUAL(s5.cmp(s5), 0);
    testCmp(s1, s2, s4);
    testCmp(s1, s2, s5);
    testCmp(s2, s4, s5);

    {
        Int64ResultNode i1(-1), i2(0), i3(1), i4(0x80000000lu);
        EXPECT_EQUAL(i1.cmp(i1), 0);
        EXPECT_EQUAL(i2.cmp(i2), 0);
        EXPECT_EQUAL(i3.cmp(i3), 0);
        testCmp(i1, i2, i3);
        testCmp(i1, i2, i4);
    }

    {
        FloatResultNode i1(-1), i2(0), i3(1), notanumber(std::nan("")),
            minusInf(-INFINITY), plussInf(INFINITY);
        EXPECT_EQUAL(i1.cmp(i1), 0);
        EXPECT_EQUAL(i2.cmp(i2), 0);
        EXPECT_EQUAL(i3.cmp(i3), 0);
        EXPECT_EQUAL(minusInf.cmp(minusInf), 0);
        EXPECT_EQUAL(plussInf.cmp(plussInf), 0);
        EXPECT_EQUAL(notanumber.cmp(notanumber), 0);
        testCmp(i1, i2, i3);
        testCmp(minusInf, i1, plussInf);
        testCmp(minusInf, i2, plussInf);
        testCmp(minusInf, i3, plussInf);
        testCmp(notanumber, i2, i3);
        testCmp(notanumber, i2, plussInf);
        testCmp(notanumber, minusInf, plussInf);
    }
    {
        FloatBucketResultNode
            i1(-1, 3), i2(188000, 188500), i3(1630000, 1630500),
            notanumber(-std::nan(""), std::nan("")), inf(-INFINITY, INFINITY);
        EXPECT_EQUAL(i1.cmp(i1), 0);
        EXPECT_EQUAL(i2.cmp(i2), 0);
        EXPECT_EQUAL(notanumber.cmp(notanumber), 0);
        EXPECT_EQUAL(inf.cmp(inf), 0);

        testCmp(i1, i2, i3);
        testCmp(inf, i1, i2);
        testCmp(notanumber, i2, i3);
        testCmp(notanumber, i1, i2);
        testCmp(notanumber, inf, i1);
    }
}

void testStreaming(const Identifiable &v) {
    nbostream os;
    NBOSerializer nos(os);
    nos << v;
    Identifiable::UP s = Identifiable::create(nos);
    ASSERT_TRUE(s.get() != NULL);
    ASSERT_TRUE(v.cmp(*s) == 0);
    nbostream os2, os3;
    NBOSerializer nos2(os2), nos3(os3);
    nos2 << v;
    nos3 << *s;

    EXPECT_EQUAL(os2.size(), os3.size());
    ASSERT_TRUE(os2.size() == os3.size());
    EXPECT_EQUAL(0, memcmp(os2.c_str(), os3.c_str(), os3.size()));
}

TEST("testTimeStamp") {
    TimeStampFunctionNode t1;
    testStreaming(t1);
}

namespace {

std::string
getVespaChecksumV2(const std::string& ymumid, int fid, const std::string& flags_str)
{
    if (fid == 6 || fid == 0 || fid == 5) {
        return 0;
    }

    std::list<char> flags_list;
    flags_list.clear();
    for (unsigned int i = 0; i< flags_str.length();i++)
      if (isalpha(flags_str[i]))
        flags_list.push_back(flags_str[i]);
    flags_list.sort();

    std::string new_flags_str ="";
    std::list<char>::iterator it;
    for (it = flags_list.begin();it!=flags_list.end();it++)
        new_flags_str += *it;

    uint32_t networkFid = htonl(fid);

    int length = ymumid.length()+
                 sizeof(networkFid)+
                 new_flags_str.length();

    unsigned char buffer[length];
    memset(buffer, 0x00, length);
    memcpy(buffer, ymumid.c_str(), ymumid.length());
    memcpy(buffer + ymumid.length(),
           (const char*)&networkFid, sizeof(networkFid));
    memcpy(buffer+ymumid.length()+sizeof(networkFid), new_flags_str.c_str(),
           new_flags_str.length());

    return std::string((char*)buffer, length);
}
}  // namespace

TEST("testMailChecksumExpression") {
    document::TestDocMan testDocMan;

    int folder = 32;
    std::string flags = "RWA";
    std::string ymumid = "barmuda";

    document::Document::UP doc = testDocMan.createDocument("foo", "userdoc:footype:1234:" + ymumid);
    document::WeightedSetFieldValue ws(doc->getField("byteweightedset").getDataType());

    for (uint32_t i = 0; i < flags.size(); i++) {
        ws.add(document::ByteFieldValue(flags[i]));
    }
    doc->setValue("headerval", document::IntFieldValue(folder));
    doc->setValue("byteweightedset", ws);

    std::unique_ptr<CatFunctionNode> e = MU<CatFunctionNode>();

    // YMUMID
    e->appendArg(MU<GetDocIdNamespaceSpecificFunctionNode>(MU<StringResultNode>()));

    // Folder
    e->appendArg(MU<DocumentFieldNode>("headerval"));

    // Flags
    e->appendArg(MU<SortFunctionNode>(MU<DocumentFieldNode>("byteweightedset")));

    MD5BitFunctionNode node(std::move(e), 32);

    CatFunctionNode &cfn = static_cast<CatFunctionNode&>(*node.expressionNodeVector()[0]);
    MultiArgFunctionNode::ExpressionNodeVector &xe = cfn.expressionNodeVector();

    for (uint32_t i = 0; i < xe.size(); i++) {
        DocumentAccessorNode* rf = dynamic_cast<DocumentAccessorNode *>(xe[i].get());
        if (rf) {
            rf->setDocType(doc->getType());
            rf->prepare(true);
            rf->setDoc(*doc);
        } else {
            MultiArgFunctionNode * mf = dynamic_cast<MultiArgFunctionNode *>(xe[i].get());
            MultiArgFunctionNode::ExpressionNodeVector& se = mf->expressionNodeVector();
            for (uint32_t j = 0; j < se.size(); j++) {
                DocumentAccessorNode* tf = dynamic_cast<DocumentAccessorNode *>(se[j].get());
                tf->setDocType(doc->getType());
                tf->prepare(true);
                tf->setDoc(*doc);
            }
        }
    }
    // SortFunctionNode & sfn = static_cast<SortFunctionNode&>(*xe[1]);
    // sfn.prepare(false);
    cfn.prepare(false);

    cfn.execute();
    ConstBufferRef ref = static_cast<const RawResultNode &>(cfn.getResult()).get();

    std::string cmp = getVespaChecksumV2(ymumid, folder, flags);

    EXPECT_EQUAL(ref.size(), 14u);
    EXPECT_EQUAL(cmp.size(), ref.size());

    for (uint32_t i = 0; i < ref.size(); i++) {
        std::cerr << i << ": " << (int)ref.c_str()[i] << "/" << (int)cmp[i]
                  << "\n";
    }

    EXPECT_TRUE(memcmp(cmp.c_str(), ref.c_str(), cmp.size()) == 0);

    node.prepare(true);
    node.execute();

    ConstBufferRef ref2 =
        static_cast<const RawResultNode &>(node.getResult()).get();

    for (uint32_t i = 0; i < ref2.size(); i++) {
        std::cerr << i << ": " << (int)ref2.c_str()[i] << "\n";
    }
}

TEST("testDebugFunction") {
    {
        std::unique_ptr<AddFunctionNode> add = MU<AddFunctionNode>();
        add->appendArg(MU<ConstantNode>(MU<Int64ResultNode>(3)));
        add->appendArg(MU<ConstantNode>(MU<Int64ResultNode>(4)));
        DebugWaitFunctionNode n(std::move(add), 1.3, false);
        n.prepare(false);

        FastOS_Time time;
        time.SetNow();
        n.execute();
        EXPECT_TRUE(time.MilliSecsToNow() > 1000.0);
        EXPECT_EQUAL(static_cast<const Int64ResultNode &>(n.getResult()).get(), 7);
    }
    {
        std::unique_ptr<AddFunctionNode> add = MU<AddFunctionNode>();
        add->appendArg(MU<ConstantNode>(MU<Int64ResultNode>(3)));
        add->appendArg(MU<ConstantNode>(MU<Int64ResultNode>(4)));
        DebugWaitFunctionNode n(std::move(add), 1.3, true);
        n.prepare(false);

        FastOS_Time time;
        time.SetNow();
        n.execute();
        EXPECT_TRUE(time.MilliSecsToNow() > 1000.0);
        EXPECT_EQUAL(static_cast<const Int64ResultNode &>(n.getResult()).get(), 7);
    }
}

template<typename V>
ResultNode::UP
createIntRV(std::vector<int64_t> values) {
    using T = typename V::BaseType;
    std::unique_ptr<V> r = MU<V>();
    for (int64_t v : values) {
        r->push_back(T(v));
    }
    return r;
}

TEST("testDivExpressions") {
    {
        StrLenFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const Int64ResultNode &>(e.getResult()).get(), 6);
    }
    {
        NormalizeSubjectFunctionNode e(MU<ConstantNode>(MU<StringResultNode>("Re: Your mail")));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const StringResultNode &>(e.getResult()).get(), "Your mail");
    }
    {
        NormalizeSubjectFunctionNode e(MU<ConstantNode>(MU<StringResultNode>("Your mail")));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const StringResultNode &>(e.getResult()).get(), "Your mail");
    }
    {
        StrCatFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.appendArg(MU<ConstantNode>(MU<StringResultNode>("ARG 2")));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const StringResultNode &>(e.getResult()).get(), "238686ARG 2");
    }

    {
        ToStringFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(strcmp(static_cast<const StringResultNode &>(e.getResult()).get().c_str(), "238686"), 0);
    }

    {
        ToRawFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(strcmp(static_cast<const RawResultNode &>(e.getResult()).get().c_str(), "238686"), 0);
    }

    {
        CatFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 8u);
    }
    {
        CatFunctionNode e(MU<ConstantNode>(MU<Int32ResultNode>(23886)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 4u);
    }
    {
        const uint8_t buf[4] = { 0, 0, 0, 7 };
        MD5BitFunctionNode e(MU<ConstantNode>(MU<RawResultNode>(buf, sizeof(buf))), 16*8);
        e.prepare(false);
        e.execute();
        ASSERT_TRUE(e.getResult().getClass().inherits(RawResultNode::classId));
        const RawResultNode &r(static_cast<const RawResultNode &>(e.getResult()));
        EXPECT_EQUAL(r.get().size(), 16u);
    }
    {
        const uint8_t buf[4] = { 0, 0, 0, 7 };
        MD5BitFunctionNode e(MU<ConstantNode>(MU<RawResultNode>(buf, sizeof(buf))), 2*8);
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 2u);
    }
    {
        const uint8_t buf[4] = { 0, 0, 0, 7 };
        XorBitFunctionNode e(MU<ConstantNode>(MU<RawResultNode>(buf, sizeof(buf))), 1*8);
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 1u);
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().c_str()[0], 0x7);
    }
    {
        const uint8_t buf[4] = { 6, 0, 7, 7 };
        XorBitFunctionNode e(MU<ConstantNode>(MU<RawResultNode>(buf, sizeof(buf))), 2*8);
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 2u);
        EXPECT_EQUAL((int)static_cast<const RawResultNode &>(e.getResult()).get().c_str()[0], 0x1);
        EXPECT_EQUAL((int)static_cast<const RawResultNode &>(e.getResult()).get().c_str()[1], 0x7);
    }
    {
        const uint8_t wantedBuf[14]  =
            { 98, 97, 114, 109, 117, 100, 97, 0, 0, 0, 32, 65, 82, 87 };
        const uint8_t md5facit[16] =
            { 0x22, 0x5, 0x22, 0x1c, 0x49, 0xff, 0x90, 0x25, 0xad, 0xbf,
              0x4e, 0x51, 0xdb, 0xca, 0x2a, 0xc5 };
        const uint8_t thomasBuf[22] =
            { 0, 0, 0, 7, 98, 97, 114, 109, 117, 100, 97, 0, 0, 0, 32, 0,
              0, 0, 3, 65, 82, 87 };
        const uint8_t currentBuf[26] =
            { 0, 0, 0, 22, 0, 0, 0, 7, 98, 97, 114, 109, 117, 100, 97, 0,
              0, 0, 32, 0 , 0, 0, 3, 65, 82, 87 };

        MD5BitFunctionNode e(MU<ConstantNode>(MU<RawResultNode>(wantedBuf, sizeof(wantedBuf))), 16*8);
        e.prepare(false);
        e.execute();
        ASSERT_TRUE(e.getResult().getClass().inherits(RawResultNode::classId));
        const RawResultNode &r(static_cast<const RawResultNode &>(e.getResult()));
        EXPECT_EQUAL(r.get().size(), 16u);
        uint8_t md5[16];
        fastc_md5sum(currentBuf, sizeof(currentBuf), md5);
        EXPECT_TRUE(memcmp(r.get().data(), md5, sizeof(md5)) != 0);
        fastc_md5sum(wantedBuf, sizeof(wantedBuf), md5);
        EXPECT_TRUE(memcmp(r.get().data(), md5, sizeof(md5)) == 0);
        fastc_md5sum(thomasBuf, sizeof(thomasBuf), md5);
        EXPECT_TRUE(memcmp(r.get().data(), md5, sizeof(md5)) != 0);

        std::unique_ptr<CatFunctionNode> cat = MU<CatFunctionNode>(MU<ConstantNode>(MU<StringResultNode>("barmuda")));
        cat->appendArg(MU<ConstantNode>(MU<Int32ResultNode>(32)));
        cat->appendArg(MU<SortFunctionNode>(MU<ConstantNode>(createIntRV<Int8ResultNodeVector>({87, 65, 82}))));

        MD5BitFunctionNode finalCheck(std::move(cat), 32);
        finalCheck.prepare(false);
        finalCheck.execute();
        const RawResultNode &rr(static_cast<const RawResultNode &>(finalCheck.getResult()));
        EXPECT_EQUAL(rr.get().size(), 4u);
        fastc_md5sum(wantedBuf, sizeof(wantedBuf), md5);
        EXPECT_TRUE(memcmp(md5facit, md5, sizeof(md5)) == 0);
        EXPECT_TRUE(memcmp(rr.get().data(), md5, rr.get().size()) == 0);
    }
    {
        CatFunctionNode e(MU<ConstantNode>(MU<Int16ResultNode>(23886)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 2u);
    }
    {
        CatFunctionNode e(MU<ConstantNode>(createIntRV<Int8ResultNodeVector>({86, 14})));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 1*2u);
    }
    {
        CatFunctionNode e(MU<ConstantNode>(createIntRV<Int32ResultNodeVector>({238686,2133214})));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(static_cast<const RawResultNode &>(e.getResult()).get().size(), 4*2u);
    }
    {
        NumElemFunctionNode e(MU<ConstantNode>(MU<Int64ResultNode>(238686)));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(e.getResult().getInteger(), 1);
    }
    {
        NumElemFunctionNode e(MU<ConstantNode>(createIntRV<Int32ResultNodeVector>({238686,2133214})));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(e.getResult().getInteger(), 2);
    }
    {
        NumElemFunctionNode e(MU<ConstantNode>(createIntRV<Int32ResultNodeVector>({238686,2133214})));
        e.prepare(false);
        e.execute();
        EXPECT_EQUAL(e.getResult().getInteger(), 2);
    }
}

bool test1MultivalueExpression(const MultiArgFunctionNode &exprConst, ExpressionNode::UP mv,
                               const ResultNode & expected)
{
   MultiArgFunctionNode & expr(const_cast<MultiArgFunctionNode &>(exprConst));
   expr.appendArg(std::move(mv));
   expr.prepare(false);
   bool ok = EXPECT_TRUE(expr.execute()) &&
             EXPECT_EQUAL(0, expr.getResult().cmp(expected));
   if (!ok) {
       std::cerr << "Expected:" << expected.asString() << std::endl
                 << "Got: " << expr.getResult().asString() << std::endl;
   }
   return ok;
}

bool test1MultivalueExpressionException(const MultiArgFunctionNode & exprConst,
                                        ExpressionNode::UP mv,
                                        const char * expected) {
   try {
       test1MultivalueExpression(exprConst, std::move(mv), NullResultNode());
       return EXPECT_TRUE(false);
   } catch (std::runtime_error & e) {
       return EXPECT_TRUE(std::string(e.what()).find(expected) != std::string::npos);
   }
}

TEST("testMultivalueExpression") {
    std::vector<int64_t> IV = {7, 17, 117};

   EXPECT_TRUE(test1MultivalueExpression(AddFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7 + 17 + 117)));
   EXPECT_TRUE(test1MultivalueExpression(MultiplyFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7 * 17 * 117)));
   EXPECT_TRUE(test1MultivalueExpressionException(DivideFunctionNode(),
                                                  MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                                  "DivideFunctionNode"));
   EXPECT_TRUE(test1MultivalueExpressionException(ModuloFunctionNode(),
                                                  MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                                  "ModuloFunctionNode"));
   EXPECT_TRUE(test1MultivalueExpression(MinFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7)));
   EXPECT_TRUE(test1MultivalueExpression(MaxFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(117)));

   EXPECT_TRUE(
           test1MultivalueExpression(
                   FixedWidthBucketFunctionNode()
                   .setWidth(Int64ResultNode(1)),
                   MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                   IntegerBucketResultNodeVector()
                   .push_back(IntegerBucketResultNode(7,8))
                   .push_back(IntegerBucketResultNode(17,18))
                   .push_back(IntegerBucketResultNode(117,118))));

   EXPECT_TRUE(
           test1MultivalueExpression(
                   RangeBucketPreDefFunctionNode()
                   .setBucketList(
                           IntegerBucketResultNodeVector()
                           .push_back(IntegerBucketResultNode(0,10))
                           .push_back(IntegerBucketResultNode(20,30))
                           .push_back(IntegerBucketResultNode(100,120))),
                   MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                   IntegerBucketResultNodeVector()
                   .push_back(IntegerBucketResultNode(0,10))
                   .push_back(IntegerBucketResultNode(0,0))
                   .push_back(IntegerBucketResultNode(100,120))));

   EXPECT_TRUE(
           test1MultivalueExpression(
                   TimeStampFunctionNode()
                   .setTimePart(TimeStampFunctionNode::Second),
                   MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                   IntegerResultNodeVector()
                   .push_back(Int64ResultNode(7))
                   .push_back(Int64ResultNode(17))
                   .push_back(Int64ResultNode(117%60))));

   EXPECT_TRUE(
           test1MultivalueExpression(NegateFunctionNode(),
                                     MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                     IntegerResultNodeVector()
                                     .push_back(Int64ResultNode(-7))
                                     .push_back(Int64ResultNode(-17))
                                     .push_back(Int64ResultNode(-117))));
   EXPECT_TRUE(test1MultivalueExpression(SortFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         IntegerResultNodeVector()
                                         .push_back(Int64ResultNode(7))
                                         .push_back(Int64ResultNode(17))
                                         .push_back(Int64ResultNode(117))));
   EXPECT_TRUE(test1MultivalueExpression(ReverseFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         IntegerResultNodeVector()
                                         .push_back(Int64ResultNode(117))
                                         .push_back(Int64ResultNode(17))
                                         .push_back(Int64ResultNode(7))));
   EXPECT_TRUE(test1MultivalueExpression(SortFunctionNode(),
                                         MU<ReverseFunctionNode>(MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV))),
                                         IntegerResultNodeVector()
                                         .push_back(Int64ResultNode(7))
                                         .push_back(Int64ResultNode(17))
                                         .push_back(Int64ResultNode(117))));
   EXPECT_TRUE(test1MultivalueExpression(AndFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7 & 17 & 117)));
   EXPECT_TRUE(test1MultivalueExpression(OrFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7 | 17 | 117)));
   EXPECT_TRUE(test1MultivalueExpression(XorFunctionNode(),
                                         MU<ConstantNode>(createIntRV<Int64ResultNodeVector>(IV)),
                                         Int64ResultNode(7 ^ 17 ^ 117)));
}

ExpressionNode::UP createScalarInt(int64_t v) { return MU<ConstantNode>(MU<Int64ResultNode>(v)); }
ExpressionNode::UP createScalarFloat(double v) { return MU<ConstantNode>(MU<FloatResultNode>(v)); }
ExpressionNode::UP createScalarString(const char * v) { return MU<ConstantNode>(MU<StringResultNode>(v)); }
ExpressionNode::UP createScalarRaw(const char * v) { return MU<ConstantNode>(MU<RawResultNode>(v, strlen(v))); }

TEST("testArithmeticNodes") {
    AttributeGuard attr1 = createInt64Attribute();
    constexpr int64_t I1 = 1, I2 = 2;
    constexpr double F1 = 1.1, F2 = 9.9;
    constexpr const char * S2 = "2";

    AddFunctionNode add1;
    add1.appendArg(createScalarInt(I1));
    add1.appendArg(createScalarInt(I2));
    ExpressionTree et(add1);

    ExpressionTree::Configure treeConf;
    et.select(treeConf, treeConf);

    EXPECT_TRUE(et.getResult().getClass().inherits(IntegerResultNode::classId));
    EXPECT_TRUE(et.ExpressionNode::execute());
    EXPECT_EQUAL(et.getResult().getInteger(), 3);
    EXPECT_TRUE(et.ExpressionNode::execute());
    EXPECT_EQUAL(et.getResult().getInteger(), 3);
    AddFunctionNode add2;
    add2.appendArg(createScalarInt(I1));
    add2.appendArg(createScalarFloat(F2));
    add2.prepare(false);
    EXPECT_TRUE(add2.getResult().getClass().inherits(FloatResultNode::classId));
    AddFunctionNode add3;
    add3.appendArg(createScalarInt(I1));
    add3.appendArg(createScalarString(S2));
    add3.prepare(false);
    EXPECT_TRUE(add3.getResult().getClass().inherits(IntegerResultNode::classId));
    AddFunctionNode add4;
    add4.appendArg(createScalarInt(I1));
    add4.appendArg(createScalarRaw(S2));
    add4.prepare(false);
    EXPECT_TRUE(add4.getResult().getClass().inherits(IntegerResultNode::classId));
    AddFunctionNode add5;
    add5.appendArg(createScalarInt(I1));
    add5.appendArg(MU<AttributeNode>(*attr1));
    add5.prepare(false);
    EXPECT_TRUE(add5.getResult().getClass().inherits(IntegerResultNode::classId));
    AddFunctionNode add6;
    add6.appendArg(createScalarFloat(F1));
    add6.appendArg(MU<AttributeNode>(*attr1));
    add6.prepare(false);
    EXPECT_TRUE(add6.getResult().getClass().inherits(FloatResultNode::classId));
}

void testArith(MultiArgFunctionNode &op, ExpressionNode::UP arg1, ExpressionNode::UP arg2,
               int64_t intResult, double floatResult)
{
    op.appendArg(std::move(arg1));
    op.appendArg(std::move(arg2));
    op.prepare(false);
    op.execute();
    EXPECT_EQUAL(intResult, op.getResult().getInteger());
    ASSERT_TRUE(intResult == op.getResult().getInteger());
    EXPECT_EQUAL(floatResult, op.getResult().getFloat());
}

void testAdd(ExpressionNode::UP arg1, ExpressionNode::UP arg2,
             int64_t intResult, double floatResult)
{
    AddFunctionNode add;
    testArith(add, std::move(arg1), std::move(arg2), intResult, floatResult);
}

void testMultiply(ExpressionNode::UP arg1, ExpressionNode::UP arg2,
                  int64_t intResult, double floatResult)
{
    MultiplyFunctionNode add;
    testArith(add, std::move(arg1), std::move(arg2), intResult, floatResult);
}

void testDivide(ExpressionNode::UP arg1, ExpressionNode::UP arg2,
                int64_t intResult, double floatResult)
{
    DivideFunctionNode add;
    testArith(add, std::move(arg1), std::move(arg2), intResult, floatResult);
}

void testModulo(ExpressionNode::UP arg1, ExpressionNode::UP arg2,
                int64_t intResult, double floatResult)
{
    ModuloFunctionNode add;
    testArith(add, std::move(arg1), std::move(arg2), intResult, floatResult);
}

ExpressionNode::UP
createVectorInt(const std::vector<double> & v) {
    std::unique_ptr<IntegerResultNodeVector> r = MU<IntegerResultNodeVector>();
    for (double d : v) {
        r->push_back(Int64ResultNode(static_cast<int64_t>(d)));
    }
    return MU<ConstantNode>(std::move(r));
}
ExpressionNode::UP
createVectorFloat(const std::vector<double> & v) {
    std::unique_ptr<FloatResultNodeVector> r = MU<FloatResultNodeVector>();
    for (double d : v) {
        r->push_back(FloatResultNode(d));
    }
    return MU<ConstantNode>(std::move(r));
}

void testArithmeticArguments(NumericFunctionNode &function,
                             const std::vector<double> & arg1,
                             const std::vector<double> & arg2,
                             const std::vector<double> & result,
                             double flattenResult)
{
    IntegerResultNodeVector ir;
    for (size_t i(0), m(result.size()); i<m; i++) {
        ir.push_back(Int64ResultNode((int64_t)result[i]));
    }
    FloatResultNodeVector fr;
    for (size_t i(0), m(result.size()); i<m; i++) {
        fr.push_back(FloatResultNode(result[i]));
    }
    function.appendArg(createScalarInt(arg1[0])).appendArg(createScalarInt(arg2[0]));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(Int64ResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getInteger(), static_cast<int64_t>(result[0]));

    function.reset();

    function.appendArg(createScalarInt(arg1[0])).appendArg(createScalarFloat(arg2[0]));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getFloat(), result[0]);

    function.reset();

    function.appendArg(createScalarFloat(arg1[0])).appendArg(createScalarInt(arg2[0]));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getFloat(), result[0]);

    function.reset();

    function.appendArg(createScalarFloat(arg1[0])).appendArg(createScalarFloat(arg2[0]));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getFloat(), result[0]);

    function.reset();

    function.appendArg(createVectorInt(arg1));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(Int64ResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getInteger(), static_cast<int64_t>(flattenResult));

    function.reset();

    function.appendArg(createVectorFloat(arg1));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNode::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_EQUAL(function.getResult().getFloat(), flattenResult);

    function.reset();

    function.appendArg(createVectorInt(arg1)).appendArg(createVectorInt(arg2));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(IntegerResultNodeVector::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_TRUE(function.getResult().getClass().equal(IntegerResultNodeVector::classId));
    EXPECT_EQUAL(static_cast<const IntegerResultNodeVector &>(function.getResult()).size(), 7u);
    EXPECT_EQUAL(0, function.getResult().cmp(ir));

    function.reset();

    function.appendArg(createVectorFloat(arg1)).appendArg(createVectorFloat(arg2));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_EQUAL(static_cast<const FloatResultNodeVector &>(function.getResult()).size(), 7u);
    EXPECT_EQUAL(0, function.getResult().cmp(fr));

    function.reset();

    function.appendArg(createVectorInt(arg1)).appendArg(createVectorFloat(arg2));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_EQUAL(static_cast<const FloatResultNodeVector &>(function.getResult()).size(), 7u);
    EXPECT_EQUAL(0, function.getResult().cmp(fr));

    function.reset();

    function.appendArg(createVectorFloat(arg1)).appendArg(createVectorInt(arg2));
    function.prepare(false);
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_TRUE(function.execute());
    EXPECT_TRUE(function.getResult().getClass().equal(FloatResultNodeVector::classId));
    EXPECT_EQUAL(static_cast<const FloatResultNodeVector &>(function.getResult()).size(), 7u);
    EXPECT_EQUAL(0, function.getResult().cmp(fr));
}

TEST("testArithmeticOperations") {
    constexpr int64_t I1 = 1793253241;
    constexpr int64_t I2 = 1676521321;
    constexpr double F1 = 1.1109876;
    constexpr double F2 = 9.767681239;

    testAdd(createScalarInt(I1), createScalarInt(I2), 3469774562ull, 3469774562ull);
    testAdd(createScalarInt(I1), createScalarFloat(F2), 1793253251ull, 1793253250.767681239);
    testAdd(createScalarFloat(F1), createScalarFloat(F2), 11, 10.878668839 );
    testMultiply(createScalarInt(I1), createScalarInt(I2), 3006427292488851361ull, 3006427292488851361ull);
    testMultiply(createScalarInt(I1), createScalarFloat(F2), 17515926039ull, 1793253241.0*9.767681239);
    testMultiply(createScalarFloat(F1), createScalarFloat(F2), 11, 10.8517727372816364 );

    std::vector<double> a(5), b(7);
    a[0] = b[0] = 1;
    a[1] = b[1] = 2;
    a[2] = b[2] = 3;
    a[3] = b[3] = 4;
    a[4] = b[4] = 5;
    b[5] = 6;
    b[6] = 7;
    std::vector<double> r(7);
    {
        r[0] = a[0] + b[0];
        r[1] = a[1] + b[1];
        r[2] = a[2] + b[2];
        r[3] = a[3] + b[3];
        r[4] = a[4] + b[4];
        r[5] = a[0] + b[5];
        r[6] = a[1] + b[6];
        AddFunctionNode f;
        testArithmeticArguments(f, a, b, r, a[0]+a[1]+a[2]+a[3]+a[4]);
    }
    {
        r[0] = a[0] * b[0];
        r[1] = a[1] * b[1];
        r[2] = a[2] * b[2];
        r[3] = a[3] * b[3];
        r[4] = a[4] * b[4];
        r[5] = a[0] * b[5];
        r[6] = a[1] * b[6];
        MultiplyFunctionNode f;
        testArithmeticArguments(f, a, b, r, a[0]*a[1]*a[2]*a[3]*a[4]);
    }
}

ExpressionNode::UP createCountAggr(int64_t initial) { return MU<CountAggregationResult>(initial); }
ExpressionNode::UP createMinAggr(const ResultNode & initial) { return MU<MinAggregationResult>(initial); }

constexpr int64_t I1 = 7, I2 = 3, I4 = 22;

ExpressionNode::UP createSumAggr() {
    std::unique_ptr<SumAggregationResult> s = MU<SumAggregationResult>();
    AggregationResult::Configure conf;
    s->setExpression(createScalarInt(I4)).select(conf, conf);
    s->aggregate(0, 0);
    return s;
}

TEST("testAggregatorsInExpressions") {
    Int64ResultNode r1(I1);
    Int64ResultNode r2(I4);


    testAdd(createScalarInt(I1), createCountAggr(I2), 10, 10);
    testMultiply(createScalarInt(I1), createCountAggr(I2), 21, 21);
    testMultiply(createCountAggr(I2), createSumAggr(), 66, 66);
    testDivide(createSumAggr(), createCountAggr(I2), 7, 7);
    testDivide(createSumAggr(), createScalarInt(I1), 3, 3);
    testModulo(createSumAggr(), createCountAggr(I2), 1, 1);
    testModulo(createSumAggr(), createScalarInt(I1), 1, 1);

    testAdd(MU<MinAggregationResult>(r2), createScalarInt(I1), 29, 29);
    testAdd(MU<MinAggregationResult>(r2), MU<MaxAggregationResult>(r1), 29, 29);

    AggregationResult::Configure conf;
    XorAggregationResult *x = new XorAggregationResult();
    x->setExpression(createScalarInt(I4)).select(conf, conf);
    x->aggregate(0, 0);
    testAdd(ExpressionNode::UP(x), createScalarInt(I1), 29, 29);

    AverageAggregationResult *avg = new AverageAggregationResult();
    avg->setExpression(createScalarInt(I4)).select(conf, conf);
    avg->aggregate(0, 0);
    testAdd(ExpressionNode::UP(avg), createScalarInt(I1), 29, 29);
}

void testAggregationResult(AggregationResult & aggr, const AggrGetter & g,
                           const ResultNode & v, const ResultNode & i,
                           const ResultNode & m, const ResultNode & s) {
    AggregationResult::Configure conf;
    aggr.setExpression(MU<ConstantNode>(ResultNode::UP(v.clone()))).select(conf, conf);
    EXPECT_TRUE(g(aggr).getClass().equal(i.getClass().id()));
    EXPECT_EQUAL(0, i.cmp(g(aggr)));
    aggr.aggregate(0,0);
    EXPECT_TRUE(g(aggr).getClass().equal(i.getClass().id()));
    EXPECT_EQUAL(0, m.cmp(g(aggr)));
    aggr.aggregate(1,0);
    EXPECT_TRUE(g(aggr).getClass().equal(i.getClass().id()));
    EXPECT_EQUAL(0, s.cmp(g(aggr)));
}

TEST("testAggregationResults") {
    struct SumGetter : AggrGetter {
        virtual const ResultNode &operator()(const AggregationResult & r) const
        { return static_cast<const SumAggregationResult &>(r).getSum(); }
    };
    SumAggregationResult sum;
    testAggregationResult(sum, SumGetter(), Int64ResultNode(7),
                          Int64ResultNode(0), Int64ResultNode(7),
                          Int64ResultNode(14));
    testAggregationResult(sum, SumGetter(), FloatResultNode(7.77),
                          FloatResultNode(0), FloatResultNode(7.77),
                          FloatResultNode(15.54));
    IntegerResultNodeVector v;
    v.push_back(Int64ResultNode(7)).push_back(Int64ResultNode(8));
    testAggregationResult(sum, SumGetter(), v, Int64ResultNode(0),
                          Int64ResultNode(15), Int64ResultNode(30));
    testAggregationResult(sum, SumGetter(), FloatResultNode(7.77),
                          FloatResultNode(0), FloatResultNode(7.77),
                          FloatResultNode(15.54));
}

TEST("testGrouping") {
    AttributeGuard attr1 = createInt64Attribute();
    ExpressionNode::UP result1(new CountAggregationResult());
    (static_cast<AggregationResult &>(*result1)).setExpression(MU<AttributeNode>(*attr1));
    ExpressionNode::UP result2( new SumAggregationResult());
    (static_cast<AggregationResult &>(*result2)).setExpression(MU<AttributeNode>(*attr1));

    Grouping grouping;
    grouping.setFirstLevel(0)
            .setLastLevel(1)
            .addLevel(std::move(GroupingLevel()
                                        .setExpression(MU<AttributeNode>(*attr1))
                                        .addResult(std::move(result1))
                                        .addResult(std::move(result2))));

    grouping.configureStaticStuff(ConfigureStaticParams(0, 0));
    grouping.aggregate(0u, 10u);
    const Group::GroupList &groups = grouping.getRoot().groups();
    EXPECT_EQUAL(grouping.getRoot().getChildrenSize(), 9u);
    ASSERT_TRUE(groups[0]->getAggregationResult(0).getClass().id() == CountAggregationResult::classId);
    ASSERT_TRUE(groups[0]->getAggregationResult(1).getClass().id() == SumAggregationResult::classId);
    EXPECT_EQUAL(groups[0]->getId().getInteger(), 6u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[0]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[0]->getAggregationResult(1)).getSum().getInteger(), 6);
    EXPECT_EQUAL(groups[1]->getId().getInteger(), 7u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[1]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[1]->getAggregationResult(1)).getSum().getInteger(), 7);
    EXPECT_EQUAL(groups[2]->getId().getInteger(), 11u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[2]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[2]->getAggregationResult(1)).getSum().getInteger(), 11);
    EXPECT_EQUAL(groups[3]->getId().getInteger(), 13u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[3]->getAggregationResult(0)).getCount(), 2u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[3]->getAggregationResult(1)).getSum().getInteger(), 26);
    EXPECT_EQUAL(groups[4]->getId().getInteger(), 17u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[4]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[4]->getAggregationResult(1)).getSum().getInteger(), 17);
    EXPECT_EQUAL(groups[5]->getId().getInteger(), 27u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[5]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[5]->getAggregationResult(1)).getSum().getInteger(), 27);
    EXPECT_EQUAL(groups[6]->getId().getInteger(), 34u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[6]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[6]->getAggregationResult(1)).getSum().getInteger(), 34);
    EXPECT_EQUAL(groups[7]->getId().getInteger(), 67891u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[7]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[7]->getAggregationResult(1)).getSum().getInteger(), 67891);
    EXPECT_EQUAL(groups[8]->getId().getInteger(), 67892u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[8]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(static_cast<const SumAggregationResult &>(groups[8]->getAggregationResult(1)).getSum().getInteger(), 67892);
    testStreaming(grouping);
}


ExpressionNode::UP
createPredefRangeBucket(const AttributeGuard & guard) {
    RangeBucketPreDefFunctionNode *predef(new RangeBucketPreDefFunctionNode(MU<AttributeNode>(*guard)));
    IntegerBucketResultNodeVector prevec;
    prevec.getVector().push_back(IntegerBucketResultNode(6,7));
    prevec.getVector().push_back(IntegerBucketResultNode(7,14));
    prevec.getVector().push_back(IntegerBucketResultNode(18,50)); //30
    prevec.getVector().push_back(IntegerBucketResultNode(80,50000000000ull)); //30
    predef->setBucketList(prevec);
    return ExpressionNode::UP(predef);
}

TEST("testGrouping2") {
    AttributeGuard attr1 = createInt64Attribute();
    ExpressionNode::UP result1( new CountAggregationResult());
    (static_cast<AggregationResult &>(*result1)).setExpression(createPredefRangeBucket(attr1));

    Grouping grouping;
    grouping.setFirstLevel(0)
            .setLastLevel(1)
            .addLevel(std::move(GroupingLevel()
                              .setExpression(createPredefRangeBucket(attr1))
                              .addResult(std::move(result1))));

    grouping.configureStaticStuff(ConfigureStaticParams(0, 0));
    grouping.aggregate(0u, 10u);
    const Group::GroupList &groups = grouping.getRoot().groups();
    EXPECT_EQUAL(grouping.getRoot().getChildrenSize(), 5u);
    ASSERT_TRUE(groups[0]->getAggregationResult(0).getClass().id() == CountAggregationResult::classId);
    EXPECT_EQUAL(groups[0]->getId().getInteger(), 0u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[0]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(groups[1]->getId().getInteger(), 0u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[1]->getAggregationResult(0)).getCount(), 1u);
    EXPECT_EQUAL(groups[2]->getId().getInteger(), 0u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[2]->getAggregationResult(0)).getCount(), 4u);
    EXPECT_EQUAL(groups[3]->getId().getInteger(), 0u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[3]->getAggregationResult(0)).getCount(), 2u);
    EXPECT_EQUAL(groups[4]->getId().getInteger(), 0u);
    EXPECT_EQUAL(static_cast<const CountAggregationResult &>(groups[4]->getAggregationResult(0)).getCount(), 2u);
    testStreaming(grouping);
}

AttributeGuard createInt64Attribute() {
    SingleInt64ExtAttribute *selectAttr1(new SingleInt64ExtAttribute("selectAttr1"));
    DocId docId(0);
    selectAttr1->addDoc(docId);
    selectAttr1->add(7);
    selectAttr1->addDoc(docId);
    selectAttr1->add(6);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(11);
    selectAttr1->addDoc(docId);
    selectAttr1->add(27);
    selectAttr1->addDoc(docId);
    selectAttr1->add(17);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(34);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67891);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67892);

    AttributeVector::SP spSelectAttr1(selectAttr1);
    AttributeGuard attr1( spSelectAttr1 );
    return attr1;
}

AttributeGuard createInt32Attribute() {
    SingleInt32ExtAttribute *selectAttr1(new SingleInt32ExtAttribute("selectAttr1"));
    DocId docId(0);
    selectAttr1->addDoc(docId);
    selectAttr1->add(7);
    selectAttr1->addDoc(docId);
    selectAttr1->add(6);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(11);
    selectAttr1->addDoc(docId);
    selectAttr1->add(27);
    selectAttr1->addDoc(docId);
    selectAttr1->add(17);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(34);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67891);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67892);

    AttributeVector::SP spSelectAttr1(selectAttr1);
    AttributeGuard attr1( spSelectAttr1 );
    return attr1;
}

AttributeGuard createInt16Attribute() {
    SingleInt16ExtAttribute *selectAttr1(new SingleInt16ExtAttribute("selectAttr1"));
    DocId docId(0);
    selectAttr1->addDoc(docId);
    selectAttr1->add(7);
    selectAttr1->addDoc(docId);
    selectAttr1->add(6);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(11);
    selectAttr1->addDoc(docId);
    selectAttr1->add(27);
    selectAttr1->addDoc(docId);
    selectAttr1->add(17);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(34);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67891);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67892);

    AttributeVector::SP spSelectAttr1(selectAttr1);
    AttributeGuard attr1( spSelectAttr1 );
    return attr1;
}

AttributeGuard createInt8Attribute() {
    SingleInt8ExtAttribute *selectAttr1(new SingleInt8ExtAttribute("selectAttr1"));
    DocId docId(0);
    selectAttr1->addDoc(docId);
    selectAttr1->add(7);
    selectAttr1->addDoc(docId);
    selectAttr1->add(6);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(11);
    selectAttr1->addDoc(docId);
    selectAttr1->add(27);
    selectAttr1->addDoc(docId);
    selectAttr1->add(17);
    selectAttr1->addDoc(docId);
    selectAttr1->add(13);
    selectAttr1->addDoc(docId);
    selectAttr1->add(34);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67891);
    selectAttr1->addDoc(docId);
    selectAttr1->add(67892);

    AttributeVector::SP spSelectAttr1(selectAttr1);
    AttributeGuard attr1( spSelectAttr1 );
    return attr1;
}

TEST("testIntegerTypes") {
    EXPECT_EQUAL(AttributeNode(*createInt8Attribute()).prepare(false)
                 .getResult().getClass().id(),
                 uint32_t(Int64ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt8Attribute())
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int8ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt16Attribute())
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt16Attribute())
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int16ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt32Attribute())
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt32Attribute())
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int32ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt64Attribute())
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNode::classId));
    EXPECT_EQUAL(AttributeNode(*createInt64Attribute())
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int64ResultNode::classId));

    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt8ExtAttribute("test"))))
            .prepare(false).getResult().getClass().id(),
            uint32_t(Int64ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt8ExtAttribute("test"))))
            .prepare(true).getResult().getClass().id(),
            uint32_t(Int8ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt16ExtAttribute("test"))))
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt16ExtAttribute("test"))))
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int16ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt32ExtAttribute("test"))))
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt32ExtAttribute("test"))))
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int32ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt64ExtAttribute("test"))))
                 .prepare(false).getResult().getClass().id(),
                 uint32_t(Int64ResultNodeVector::classId));
    EXPECT_EQUAL(AttributeNode(*AttributeGuard(AttributeVector::SP(new MultiInt64ExtAttribute("test"))))
                 .prepare(true).getResult().getClass().id(),
                 uint32_t(Int64ResultNodeVector::classId));
}

TEST("testStreamingAll") {
    testStreaming(Int64ResultNode(89));
    testStreaming(FloatResultNode(89.765));
    testStreaming(StringResultNode("Tester StringResultNode streaming"));
    testStreaming(RawResultNode("Tester RawResultNode streaming", 30));
    testStreaming(CountAggregationResult());
    testStreaming(ExpressionCountAggregationResult());
    testStreaming(SumAggregationResult());
    testStreaming(MinAggregationResult());
    testStreaming(MaxAggregationResult());
    testStreaming(AverageAggregationResult());
    testStreaming(Group());
    testStreaming(Grouping());
    testStreaming(HitsAggregationResult());
}

TEST_MAIN() { TEST_RUN_ALL(); }
