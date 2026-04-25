#ifndef CAUTH_CORE_PLATFORM_ENDPOINT_ROUTE_CACHE_HPP
#define CAUTH_CORE_PLATFORM_ENDPOINT_ROUTE_CACHE_HPP

#include "core/platform/operation_cancel.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace cauth::core::platform {

struct EndpointProbeOutcome {
    enum class Status {
        Skipped,
        Succeeded,
        Failed,
    };

    Status status = Status::Skipped;
    std::uint64_t latency_ms = 0;

    static EndpointProbeOutcome skipped() { return {}; }

    static EndpointProbeOutcome succeeded(std::uint64_t latency_ms_value) {
        return {Status::Succeeded, latency_ms_value};
    }

    static EndpointProbeOutcome failed() { return {Status::Failed, 0}; }
};

struct EndpointRouteSnapshot {
    bool has_fresh_latency = false;
    bool has_recent_success = false;
    bool has_recent_failure = false;
    std::uint64_t latency_ms = 0;
    std::uint32_t success_count = 0;
    std::uint32_t failure_count = 0;
    std::uint64_t composite_score = std::numeric_limits<std::uint64_t>::max();
};

struct ProbedRouteEntry {
    std::string endpoint;
    std::string protocol;
    std::string role;
    std::string note;
    bool latency_known = false;
    std::uint64_t latency_ms = 0;
    bool recent_success = false;
    bool recent_failure = false;
    std::uint32_t success_count = 0;
    std::uint32_t failure_count = 0;
};

struct ProbedRouteResult {
    bool ok = false;
    std::string module_status = "idle";
    std::string backend;
    std::string message;
    std::vector<ProbedRouteEntry> routes;
};

class EndpointRouteCache {
  public:
    static EndpointRouteCache& instance();

    EndpointRouteSnapshot snapshot(std::string_view group, std::string_view endpoint_key) const;
    void record_success(std::string_view group,
                        std::string_view endpoint_key,
                        std::uint64_t latency_ms);
    void record_failure(std::string_view group, std::string_view endpoint_key);
    void clear();

  private:
    EndpointRouteCache() = default;
};

std::uint64_t elapsed_milliseconds(std::chrono::steady_clock::duration duration);
EndpointProbeOutcome probe_http_endpoint(std::string_view url,
                                         std::int32_t connect_timeout_ms = 2000,
                                         std::int32_t read_timeout_ms = 2000);

inline ProbedRouteEntry make_probed_route_entry(std::string endpoint,
                                                std::string protocol,
                                                std::string role,
                                                std::string note,
                                                const EndpointRouteSnapshot& snapshot) {
    return ProbedRouteEntry{
        std::move(endpoint),
        std::move(protocol),
        std::move(role),
        std::move(note),
        snapshot.has_fresh_latency,
        snapshot.latency_ms,
        snapshot.has_recent_success,
        snapshot.has_recent_failure,
        snapshot.success_count,
        snapshot.failure_count,
    };
}

template <typename Endpoint, typename KeyFn, typename ProbeFn>
std::vector<Endpoint> rank_endpoints_by_route_health(std::string_view group,
                                                     std::vector<Endpoint> endpoints,
                                                     KeyFn&& key_fn,
                                                     ProbeFn&& probe_fn,
                                                     std::size_t max_active_probes) {
    if (endpoints.size() <= 1) {
        return endpoints;
    }

    struct Candidate {
        Endpoint endpoint;
        std::string key;
        EndpointRouteSnapshot snapshot;
        std::size_t original_index = 0;
    };

    auto& cache = EndpointRouteCache::instance();
    std::vector<Candidate> candidates;
    candidates.reserve(endpoints.size());
    for (std::size_t index = 0; index < endpoints.size(); ++index) {
        auto key = std::string{key_fn(endpoints[index])};
        auto snapshot = cache.snapshot(group, key);
        candidates.push_back(Candidate{
            std::move(endpoints[index]),
            std::move(key),
            std::move(snapshot),
            index,
        });
    }

    std::size_t probes_used = 0;
    for (auto& candidate : candidates) {
        if (current_thread_operation_cancel_requested()) {
            break;
        }
        if (candidate.snapshot.has_fresh_latency || probes_used >= max_active_probes) {
            continue;
        }

        const auto probe_result = probe_fn(candidate.endpoint);
        switch (probe_result.status) {
        case EndpointProbeOutcome::Status::Succeeded:
            cache.record_success(group, candidate.key, probe_result.latency_ms);
            candidate.snapshot = cache.snapshot(group, candidate.key);
            ++probes_used;
            break;
        case EndpointProbeOutcome::Status::Failed:
            cache.record_failure(group, candidate.key);
            candidate.snapshot = cache.snapshot(group, candidate.key);
            ++probes_used;
            break;
        case EndpointProbeOutcome::Status::Skipped:
        default:
            break;
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        const auto left_key = std::tuple{
            left.snapshot.has_recent_failure ? 1 : 0,
            left.snapshot.has_fresh_latency ? 0 : 1,
            left.snapshot.composite_score,
            left.original_index,
        };
        const auto right_key = std::tuple{
            right.snapshot.has_recent_failure ? 1 : 0,
            right.snapshot.has_fresh_latency ? 0 : 1,
            right.snapshot.composite_score,
            right.original_index,
        };
        return left_key < right_key;
    });

    std::vector<Endpoint> ranked;
    ranked.reserve(candidates.size());
    for (auto& candidate : candidates) {
        ranked.push_back(std::move(candidate.endpoint));
    }
    return ranked;
}

template <typename Endpoint, typename KeyFn>
std::vector<Endpoint> rank_endpoints_by_route_health(std::string_view group,
                                                     std::vector<Endpoint> endpoints,
                                                     KeyFn&& key_fn,
                                                     std::size_t max_active_probes = 0) {
    return rank_endpoints_by_route_health(
        group,
        std::move(endpoints),
        std::forward<KeyFn>(key_fn),
        [](const Endpoint&) { return EndpointProbeOutcome::skipped(); },
        max_active_probes);
}

} // namespace cauth::core::platform

#endif
