// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include "unaryfunctionnode.h"
#include "resultvector.h"
#include "integerresultnode.h"
#include "floatresultnode.h"
#include "stringresultnode.h"

namespace search {
namespace expression {

class RangeBucketPreDefFunctionNode : public UnaryFunctionNode
{
private:
    virtual void onPrepareResult();
    virtual bool onExecute() const;
    virtual void visitMembers(vespalib::ObjectVisitor &visitor) const;

    class Handler {
    public:
        Handler(const RangeBucketPreDefFunctionNode & rangeNode) : _predef(rangeNode.getBucketList()), _nullResult(rangeNode._nullResult) { }
        virtual ~Handler() { }
        virtual const ResultNode * handle(const ResultNode & arg) = 0;
    protected:
        const ResultNodeVector & _predef;
        const ResultNode * _nullResult;
    };
    class SingleValueHandler : public Handler {
    public:
        SingleValueHandler(const RangeBucketPreDefFunctionNode & rangeNode) :
            Handler(rangeNode)
        { }
        virtual const ResultNode * handle(const ResultNode & arg);
    };
    class MultiValueHandler : public Handler {
    public:
        MultiValueHandler(const RangeBucketPreDefFunctionNode & rangeNode) :
            Handler(rangeNode),
            _result(static_cast<ResultNodeVector &>(rangeNode.updateResult()))
        { }
        virtual const ResultNode * handle(const ResultNode & arg);
    private:
        ResultNodeVector & _result;
    };


    ResultNodeVector::CP       _predef;
    mutable const ResultNode * _result;
    const ResultNode         * _nullResult;
    std::unique_ptr<Handler>     _handler;
    static IntegerBucketResultNode   _nullIntegerResult;
    static FloatBucketResultNode     _nullFloatResult;
    static StringBucketResultNode    _nullStringResult;
    static RawBucketResultNode       _nullRawResult;

public:
    DECLARE_EXPRESSIONNODE(RangeBucketPreDefFunctionNode);
    DECLARE_NBO_SERIALIZE;
    RangeBucketPreDefFunctionNode() : UnaryFunctionNode(), _predef(), _result(NULL), _nullResult(NULL) {}
    RangeBucketPreDefFunctionNode(ExpressionNode::UP arg) : UnaryFunctionNode(std::move(arg)), _predef(), _result(NULL), _nullResult(NULL) {}
    RangeBucketPreDefFunctionNode(const RangeBucketPreDefFunctionNode & rhs);
    RangeBucketPreDefFunctionNode & operator = (const RangeBucketPreDefFunctionNode & rhs);
    virtual const ResultNode & getResult()   const { return *_result; }
    const ResultNodeVector & getBucketList() const { return *_predef; }
    ResultNodeVector       & getBucketList()       { return *_predef; }
    RangeBucketPreDefFunctionNode & setBucketList(const ResultNodeVector & predef) {
        _predef.reset(static_cast<ResultNodeVector *>(predef.clone()));
        return *this;
    }
};

}
}

