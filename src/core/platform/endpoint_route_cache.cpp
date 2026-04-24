#include "core/platform/endpoint_route_cache.hpp"
#include "core/platform/http_client.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace cauth::core::platform {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kLatencyFreshness = std::chrono::minutes{10};
constexpr auto kRecentSuccessWindow = std::chrono::minutes{10};
constexpr auto kRecentFailureWindow = std::chrono::minutes{5};
constexpr std::uint64_t kRecentFailurePenaltyMs = 1500;
constexpr std::uint64_t kHistoricalFailurePenaltyMs = 250;
constexpr std::uint64_t kRecentSuccessBonusMs = 25;

struct EndpointRouteStats {
    std::optional<std::uint64_t> latency_ms;
    Clock::time_point last_latency_at{};
    Clock::time_point last_success_at{};
    Clock::time_point last_failure_at{};
    std::uint32_t success_count = 0;
    std::uint32_t failure_count = 0;
};

std::string make_cache_key(std::string_view group, std::string_view endpoint_key) {
    auto key = std::string{group};
    key.push_back('\n');
    key.append(endpoint_key);
    return key;
}

class EndpointRouteCacheStorage {
  public:
    EndpointRouteSnapshot snapshot(std::string_view group, std::string_view endpoint_key) const {
        const auto now = Clock::now();
        std::lock_guard lock{mutex_};
        const auto it = entries_.find(make_cache_key(group, endpoint_key));
        if (it == entries_.end()) {
            return {};
        }

        const auto& entry = it->second;
        EndpointRouteSnapshot snapshot;
        snapshot.success_count = entry.success_count;
        snapshot.failure_count = entry.failure_count;
        snapshot.has_recent_success =
            entry.success_count != 0 && (now - entry.last_success_at) <= kRecentSuccessWindow;
        snapshot.has_recent_failure =
            entry.failure_count != 0 && (now - entry.last_failure_at) <= kRecentFailureWindow;
        snapshot.has_fresh_latency =
            entry.latency_ms.has_value() && (now - entry.last_latency_at) <= kLatencyFreshness;
        snapshot.latency_ms = entry.latency_ms.value_or(0);

        if (!snapshot.has_fresh_latency) {
            return snapshot;
        }

        auto score = snapshot.latency_ms;
        if (snapshot.has_recent_failure) {
            score += kRecentFailurePenaltyMs;
        } else if (snapshot.failure_count != 0) {
            score += kHistoricalFailurePenaltyMs;
        }
        if (snapshot.has_recent_success && score > kRecentSuccessBonusMs) {
            score -= kRecentSuccessBonusMs;
        }
        snapshot.composite_score = score;
        return snapshot;
    }

    void record_success(std::string_view group,
                        std::string_view endpoint_key,
                        std::uint64_t latency_ms) {
        std::lock_guard lock{mutex_};
        auto& entry = entries_[make_cache_key(group, endpoint_key)];
        entry.latency_ms = latency_ms;
        entry.last_latency_at = Clock::now();
        entry.last_success_at = entry.last_latency_at;
        if (entry.success_count != std::numeric_limits<std::uint32_t>::max()) {
            ++entry.success_count;
        }
    }

    void record_failure(std::string_view group, std::string_view endpoint_key) {
        std::lock_guard lock{mutex_};
        auto& entry = entries_[make_cache_key(group, endpoint_key)];
        entry.last_failure_at = Clock::now();
        if (entry.failure_count != std::numeric_limits<std::uint32_t>::max()) {
            ++entry.failure_count;
        }
    }

    void clear() {
        std::lock_guard lock{mutex_};
        entries_.clear();
    }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, EndpointRouteStats> entries_;
};

EndpointRouteCacheStorage& storage() {
    static EndpointRouteCacheStorage cache;
    return cache;
}

} // namespace

EndpointRouteCache& EndpointRouteCache::instance() {
    static EndpointRouteCache cache;
    return cache;
}

EndpointRouteSnapshot EndpointRouteCache::snapshot(std::string_view group,
                                                   std::string_view endpoint_key) const {
    return storage().snapshot(group, endpoint_key);
}

void EndpointRouteCache::record_success(std::string_view group,
                                        std::string_view endpoint_key,
                                        std::uint64_t latency_ms) {
    storage().record_success(group, endpoint_key, latency_ms);
}

void EndpointRouteCache::record_failure(std::string_view group, std::string_view endpoint_key) {
    storage().record_failure(group, endpoint_key);
}

void EndpointRouteCache::clear() { storage().clear(); }

std::uint64_t elapsed_milliseconds(std::chrono::steady_clock::duration duration) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return elapsed <= 0 ? 1 : static_cast<std::uint64_t>(elapsed);
}

EndpointProbeOutcome probe_http_endpoint(std::string_view url,
                                         std::int32_t connect_timeout_ms,
                                         std::int32_t read_timeout_ms) {
    if (!is_platform_http_client_available()) {
        return EndpointProbeOutcome::skipped();
    }

    HttpRequest request;
    request.url = std::string{url};
    request.connect_timeout_ms = connect_timeout_ms;
    request.read_timeout_ms = read_timeout_ms;

    const auto started = Clock::now();
    const auto response = perform_platform_http_request(request);
    const auto latency_ms = elapsed_milliseconds(Clock::now() - started);
    if (response.ok || response.status_code != 0) {
        return EndpointProbeOutcome::succeeded(latency_ms);
    }
    return EndpointProbeOutcome::failed();
}

} // namespace cauth::core::platform
