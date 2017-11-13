// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include <vespa/vespalib/testkit/testapp.h>
#include <vespa/vespalib/metrics/simple_metrics.h>

using namespace vespalib;
using namespace vespalib::metrics;

TEST("require that simple metrics gauge merge works")
{
    MergedGauge a(42), b(42), c(42);
    b.observedCount = 3;
    b.sumValue = 24.0;
    b.minValue = 7.0;
    b.maxValue = 9.0;
    b.lastValue = 8.0;

    EXPECT_EQUAL(a.observedCount, 0u);
    EXPECT_EQUAL(a.sumValue, 0.0);
    EXPECT_EQUAL(a.minValue, 0.0);
    EXPECT_EQUAL(a.maxValue, 0.0);
    EXPECT_EQUAL(a.lastValue, 0.0);
    a.merge(b);
    EXPECT_EQUAL(a.observedCount, 3u);
    EXPECT_EQUAL(a.sumValue, 24.0);
    EXPECT_EQUAL(a.minValue, 7.0);
    EXPECT_EQUAL(a.maxValue, 9.0);
    EXPECT_EQUAL(a.lastValue, 8.0);
    a.merge(b);
    EXPECT_EQUAL(a.observedCount, 6u);
    EXPECT_EQUAL(a.sumValue, 48.0);
    EXPECT_EQUAL(a.minValue, 7.0);
    EXPECT_EQUAL(a.maxValue, 9.0);
    EXPECT_EQUAL(a.lastValue, 8.0);

    c.observedCount = 2;
    c.sumValue = 11.0;
    c.minValue = 1.0;
    c.maxValue = 10.0;
    c.lastValue = 1.0;

    a.merge(c);
    EXPECT_EQUAL(a.observedCount, 8u);
    EXPECT_EQUAL(a.sumValue, 59.0);
    EXPECT_EQUAL(a.minValue, 1.0);
    EXPECT_EQUAL(a.maxValue, 10.0);
    EXPECT_EQUAL(a.lastValue, 1.0);
}

TEST_MAIN() { TEST_RUN_ALL(); }
