// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include "simple_metrics_collector.h"

#include <vespa/log/log.h>
LOG_SETUP(".simple_metrics_collector");

namespace vespalib {
namespace metrics {

using Guard = std::lock_guard<std::mutex>;

SimpleMetricsCollector::SimpleMetricsCollector(const CollectorConfig &config)
    : _metricNames(),
      _axisNames(),
      _coordValues(),
      _pointMaps(),
      _currentBucket(),
      _startTime(now_stamp()),
      _curTime(_startTime),
      _buckets(),
      _firstBucket(0),
      _maxBuckets(config.sliding_window_seconds),
      _stopFlag(false),
      _collectorThread(doCollectLoop, this)
{
    if (_maxBuckets < 1) _maxBuckets = 1;
    Point empty = pointFrom(PointMapBacking());
    assert(empty.id() == 0);
}

SimpleMetricsCollector::~SimpleMetricsCollector()
{
    _stopFlag = true;
    _collectorThread.join();
}


std::shared_ptr<MetricsCollector>
SimpleMetricsCollector::create(const CollectorConfig &config)
{
    return std::shared_ptr<MetricsCollector>(
        new SimpleMetricsCollector(config));
}

Counter
SimpleMetricsCollector::counter(const vespalib::string &name)
{
    int id = _metricNames.resolve(name);
    LOG(debug, "metric name %s -> %d", name.c_str(), id);
    return Counter(shared_from_this(), MetricIdentifier(id));
}

Gauge
SimpleMetricsCollector::gauge(const vespalib::string &name)
{
    int id = _metricNames.resolve(name);
    LOG(debug, "metric name %s -> %d", name.c_str(), id);
    return Gauge(shared_from_this(), MetricIdentifier(id));
}

Snapshot
SimpleMetricsCollector::snapshot()
{
    InternalTimeStamp startTime =
        (_buckets.size() > 0)
        ? _buckets[_firstBucket].startTime
        : _curTime;
    Bucket merger(startTime, startTime);
    for (size_t i = 0; i < _buckets.size(); i++) {
        size_t off = (_firstBucket + i) % _buckets.size();
        merger.merge(_buckets[off]);
    }

    std::vector<PointSnapshot> points;

    std::chrono::microseconds s = since_epoch(merger.startTime);
    std::chrono::microseconds e = since_epoch(merger.endTime);
    const double micro = 0.000001;
    Snapshot snap(s.count() * micro, e.count() * micro);
    {
        Guard guard(_pointMaps.lock);
        for (auto entry : _pointMaps.vec) {
             const PointMapBacking &map = entry->first.backing();
             PointSnapshot point;
             for (const PointMapBacking::value_type &kv : map) {
                 point.dimensions.emplace_back(nameFor(kv.first), valueFor(kv.second));
             }
             snap.add(point);
        }
    }
    for (const MergedCounter& entry : merger.counters) {
        size_t ni = entry.idx.name_idx;
        size_t pi = entry.idx.point_idx;
        const vespalib::string &name = _metricNames.lookup(ni);
        CounterSnapshot val(name, snap.points()[pi], entry);
        snap.add(val);
    }
    for (const MergedGauge& entry : merger.gauges) {
        size_t ni = entry.idx.name_idx;
        size_t pi = entry.idx.point_idx;
        const vespalib::string &name = _metricNames.lookup(ni);
        GaugeSnapshot val(name, snap.points()[pi], entry);
        snap.add(val);
    }
    return snap;
}

void
SimpleMetricsCollector::doCollectLoop(SimpleMetricsCollector *me)
{
    const std::chrono::milliseconds jiffy{20};
    const std::chrono::seconds oneSec{1};
    while (!me->_stopFlag) {
        std::this_thread::sleep_for(jiffy);
        InternalTimeStamp now = now_stamp();
        InternalTimeStamp::duration elapsed = now - me->_curTime;
        if (elapsed >= oneSec) {
            me->collectCurrentBucket();
        }
    }
}

void
SimpleMetricsCollector::collectCurrentBucket()
{
    InternalTimeStamp prev = _curTime;
    InternalTimeStamp curr = now_stamp();

    CurrentSamples samples;
    {
        Guard guard(_currentBucket.lock);
        swap(samples, _currentBucket);
    }

    Bucket merger(prev, curr);
    if (_buckets.size() < _maxBuckets) {
        _buckets.push_back(merger);
        _buckets.back().merge(samples);
    } else {
        merger.merge(samples);
        swap(_buckets[_firstBucket], merger);
        _firstBucket = (_firstBucket + 1) % _buckets.size();
    }
    _curTime = curr;
}

Axis
SimpleMetricsCollector::axis(const vespalib::string &name)
{
    int id = _axisNames.resolve(name);
    LOG(debug, "axis name %s -> %d", name.c_str(), id);
    return Axis(id);
}

Coordinate
SimpleMetricsCollector::coordinate(const vespalib::string &value)
{
    int id = _coordValues.resolve(value);
    LOG(debug, "coord value %s -> %d", value.c_str(), id);
    return Coordinate(id);
}

PointBuilder
SimpleMetricsCollector::pointBuilder(Point from)
{
    Guard guard(_pointMaps.lock);
    const PointMap &map = _pointMaps.vec[from.id()]->first;
    return PointBuilder(shared_from_this(), map.backing());
}

Point
SimpleMetricsCollector::pointFrom(PointMapBacking &&map)
{
    Guard guard(_pointMaps.lock);
    size_t nextId = _pointMaps.vec.size();
    auto iter_check = _pointMaps.map.emplace(std::move(map), nextId);
    if (iter_check.second) {
        LOG(debug, "new point map -> %zd / %zd", nextId, iter_check.first->second);
        _pointMaps.vec.push_back(iter_check.first);
    } else {
        LOG(debug, "found point map -> %zd", iter_check.first->second);
    }
    return Point(iter_check.first->second);
}

} // namespace vespalib::metrics
} // namespace vespalib
