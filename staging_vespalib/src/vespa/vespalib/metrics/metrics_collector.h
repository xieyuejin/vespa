// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include <memory>
#include <thread>
#include <vespa/vespalib/stllike/string.h>
#include "simple_metrics.h"
#include "name_collection.h"
#include "counter.h"
#include "gauge.h"
#include "mergers.h"
#include "snapshots.h"
#include "point.h"

namespace vespalib {
namespace metrics {


class MetricsCollector
    : public std::enable_shared_from_this<MetricsCollector>
{
public:
    virtual ~MetricsCollector() {}

    virtual Counter counter(const vespalib::string &name) = 0; // get or create
    virtual  Gauge   gauge (const vespalib::string &name) = 0; // get or create

    virtual Axis axis(const vespalib::string &name) = 0; // get or create
    virtual Coordinate coordinate(const vespalib::string &value) = 0; // get or create
    PointBuilder pointBuilder() {
        return PointBuilder(shared_from_this());
    }
    virtual PointBuilder pointBuilder(Point from) = 0;

    virtual Point pointFrom(PointMapBacking &&map) = 0;

    virtual Snapshot snapshot() = 0;

    // for use from Counter only
    virtual void add(CounterIncrement inc) = 0;

    // for use from Gauge only
    virtual void sample(GaugeMeasurement value) = 0;
};


} // namespace vespalib::metrics
} // namespace vespalib
