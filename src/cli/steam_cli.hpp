#pragma once

#include <iosfwd>

namespace cauth::cli {

void print_cli_usage();
int run_cli(int argc, char** argv, std::ostream& out, std::ostream& err);

int run_steam_auth(int argc, char** argv);
int run_steam_depot(int argc, char** argv);
int run_steam_cloud(int argc, char** argv);

} // namespace cauth::cli
