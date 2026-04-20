#include "core/version.hpp"
#include "core/version_generated.hpp"

namespace cauth::core {

Version version() noexcept {
    return Version{
        generated::version_major,
        generated::version_minor,
        generated::version_patch,
        generated::version_text,
    };
}

} // namespace cauth::core
