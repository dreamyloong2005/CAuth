#include "core/platform/endpoint_route_cache.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool test_cached_latency_prefers_fastest_endpoint() {
    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    cache.clear();

    cache.record_success("test.group", "slow", 80);
    cache.record_success("test.group", "fast", 12);
    cache.record_success("test.group", "mid", 40);

    const auto ranked = cauth::core::platform::rank_endpoints_by_route_health(
        "test.group",
        std::vector<std::string>{"slow", "mid", "fast"},
        [](const std::string& endpoint) { return endpoint; });

    cache.clear();
    return ranked.size() == 3 && ranked[0] == "fast" && ranked[1] == "mid" && ranked[2] == "slow";
}

bool test_recent_failure_penalizes_endpoint() {
    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    cache.clear();

    cache.record_success("test.group", "fast-but-flaky", 10);
    cache.record_success("test.group", "steady", 45);
    cache.record_failure("test.group", "fast-but-flaky");

    const auto ranked = cauth::core::platform::rank_endpoints_by_route_health(
        "test.group",
        std::vector<std::string>{"fast-but-flaky", "steady"},
        [](const std::string& endpoint) { return endpoint; });

    cache.clear();
    return ranked.size() == 2 && ranked[0] == "steady" && ranked[1] == "fast-but-flaky";
}

bool test_active_probe_populates_ranking() {
    auto& cache = cauth::core::platform::EndpointRouteCache::instance();
    cache.clear();

    std::size_t probe_calls = 0;
    const auto ranked = cauth::core::platform::rank_endpoints_by_route_health(
        "test.group",
        std::vector<std::string>{"unknown", "slow", "fast"},
        [](const std::string& endpoint) { return endpoint; },
        [&probe_calls](const std::string& endpoint) {
            ++probe_calls;
            if (endpoint == "slow") {
                return cauth::core::platform::EndpointProbeOutcome::succeeded(75);
            }
            if (endpoint == "fast") {
                return cauth::core::platform::EndpointProbeOutcome::succeeded(8);
            }
            return cauth::core::platform::EndpointProbeOutcome::skipped();
        },
        3);

    const auto fast_snapshot = cache.snapshot("test.group", "fast");
    const auto slow_snapshot = cache.snapshot("test.group", "slow");
    cache.clear();

    return probe_calls == 3 && ranked.size() == 3 && ranked[0] == "fast" && ranked[1] == "slow" &&
           ranked[2] == "unknown" && fast_snapshot.has_fresh_latency &&
           slow_snapshot.has_fresh_latency && fast_snapshot.latency_ms == 8 &&
           slow_snapshot.latency_ms == 75;
}

} // namespace

int main() {
    if (!test_cached_latency_prefers_fastest_endpoint()) {
        std::cerr << "endpoint route cache should rank endpoints by cached latency\n";
        return 1;
    }
    if (!test_recent_failure_penalizes_endpoint()) {
        std::cerr << "endpoint route cache should penalize recently failing endpoints\n";
        return 1;
    }
    if (!test_active_probe_populates_ranking()) {
        std::cerr << "endpoint route cache should use active probes to fill ranking data\n";
        return 1;
    }
    return 0;
}
