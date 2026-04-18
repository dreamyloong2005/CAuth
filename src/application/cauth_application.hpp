#pragma once

#include <iosfwd>

namespace cauth::application {

int print_version(std::ostream& out);
int run_doctor(std::ostream& out, std::ostream& err);

} // namespace cauth::application
