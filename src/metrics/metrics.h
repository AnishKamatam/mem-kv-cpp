#pragma once

#include <atomic>
#include <string>
#include <chrono>
#include <vector>
#include <algorithm>
#include <mutex>

struct LatencyHistogram {
    // Buckets: <1ms, <5ms, <10ms, <50ms, <100ms, >=100ms
    std::atomic<uint64_t> bucket_1ms{0};
    std::atomic<uint64_t> bucket_5ms{0};
    std::atomic<uint64_t> bucket_10ms{0};
    std::atomic<uint64_t> bucket_50ms{0};
    std::atomic<uint64_t> bucket_100ms{0};
    std::atomic<uint64_t> bucket_plus{0};
    
    // Store individual latencies for percentile calculation
    mutable std::vector<uint64_t> latency_samples;
    mutable std::mutex samples_mtx;
    static constexpr size_t MAX_SAMPLES = 10000; // Keep last 10k samples
    
    void record(uint64_t micros) {
        uint64_t millis = micros / 1000;
        
        if (millis < 1) bucket_1ms++;
        else if (millis < 5) bucket_5ms++;
        else if (millis < 10) bucket_10ms++;
        else if (millis < 50) bucket_50ms++;
        else if (millis < 100) bucket_100ms++;
        else bucket_plus++;
        
        // Store sample for percentile calculation
        {
            std::lock_guard<std::mutex> lock(samples_mtx);
            latency_samples.push_back(micros);
            if (latency_samples.size() > MAX_SAMPLES) {
                latency_samples.erase(latency_samples.begin());
            }
        }
    }
    
    // Calculate percentiles from samples
    uint64_t percentile(double p) const {
        std::lock_guard<std::mutex> lock(samples_mtx);
        if (latency_samples.empty()) return 0;
        
        std::vector<uint64_t> sorted = latency_samples;
        std::sort(sorted.begin(), sorted.end());
        
        size_t index = static_cast<size_t>(p * sorted.size());
        if (index >= sorted.size()) index = sorted.size() - 1;
        
        return sorted[index];
    }
    
    // Get histogram counts
    struct BucketCounts {
        uint64_t b1ms, b5ms, b10ms, b50ms, b100ms, bplus;
    };
    
    BucketCounts get_counts() const {
        return {
            bucket_1ms.load(),
            bucket_5ms.load(),
            bucket_10ms.load(),
            bucket_50ms.load(),
            bucket_100ms.load(),
            bucket_plus.load()
        };
    }
};

class Metrics {
public:
    static Metrics& instance() {
        static Metrics inst;
        return inst;
    }
    
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_latency_us{0};
    
    LatencyHistogram latency_histogram;
    
    // Batch statistics
    std::atomic<uint64_t> total_batches{0};
    std::atomic<uint64_t> total_batched_writes{0};
    
    std::string to_json() const {
        uint64_t hits = cache_hits.load();
        uint64_t misses = cache_misses.load();
        uint64_t total = total_requests.load();
        uint64_t latency = total_latency_us.load();
        
        double hit_rate = total > 0 ? (100.0 * hits / total) : 0.0;
        double avg_latency_us = total > 0 ? (1.0 * latency / total) : 0.0;
        
        // Calculate percentiles
        uint64_t p50_us = latency_histogram.percentile(0.50);
        uint64_t p95_us = latency_histogram.percentile(0.95);
        uint64_t p99_us = latency_histogram.percentile(0.99);
        
        // Batch statistics
        uint64_t batches = total_batches.load();
        uint64_t batched_writes = total_batched_writes.load();
        double avg_batch_size = batches > 0 ? (1.0 * batched_writes / batches) : 0.0;
        
        // Histogram buckets
        auto buckets = latency_histogram.get_counts();
        uint64_t p50_less_than_1ms = buckets.b1ms;
        uint64_t p99_tail_events = buckets.bplus; // Events >= 100ms
        
        return "{\"cache_hits\":" + std::to_string(hits) +
               ",\"cache_misses\":" + std::to_string(misses) +
               ",\"total_requests\":" + std::to_string(total) +
               ",\"hit_rate\":" + std::to_string(hit_rate) +
               ",\"avg_latency_us\":" + std::to_string(avg_latency_us) +
               ",\"p50_latency_us\":" + std::to_string(p50_us) +
               ",\"p95_latency_us\":" + std::to_string(p95_us) +
               ",\"p99_latency_us\":" + std::to_string(p99_us) +
               ",\"p50_less_than_1ms\":" + std::to_string(p50_less_than_1ms) +
               ",\"p99_tail_events\":" + std::to_string(p99_tail_events) +
               ",\"batch_avg_size\":" + std::to_string(avg_batch_size) +
               ",\"histogram\":{" +
               "\"<1ms\":" + std::to_string(buckets.b1ms) +
               ",\"<5ms\":" + std::to_string(buckets.b5ms) +
               ",\"<10ms\":" + std::to_string(buckets.b10ms) +
               ",\"<50ms\":" + std::to_string(buckets.b50ms) +
               ",\"<100ms\":" + std::to_string(buckets.b100ms) +
               ",\">=100ms\":" + std::to_string(buckets.bplus) +
               "}}";
    }
    
    void record_latency(uint64_t microseconds) {
        total_latency_us.fetch_add(microseconds);
        latency_histogram.record(microseconds);
    }
    
    void record_batch(size_t batch_size) {
        total_batches++;
        total_batched_writes.fetch_add(batch_size);
    }
    
private:
    Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
};

