#include "application/cauth_application.hpp"

#include "core/platform/session_repository_factory.hpp"
#include "core/version.hpp"

namespace cauth::application {

int print_version(std::ostream& out) {
    const auto current = cauth::core::version();
    out << "CAuth " << current.text << '\n';
    return 0;
}

int run_doctor(std::ostream& out, std::ostream& err) {
    const auto repository = cauth::core::platform::make_platform_session_repository();
    if (!repository) {
        err << "failed to create platform session repository\n";
        return 1;
    }

    out << "CAuth core: ok\n";
    return 0;
}

} // namespace cauth::application
