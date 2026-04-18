#pragma once

#include "core/session/session_repository.hpp"

#include <memory>

struct cauth_client {
    std::unique_ptr<cauth::core::session::SessionRepository> session_repository;
};
