#pragma once

#include <stdexcept>
#include <string>

namespace horelac::domain {

enum class ErrorCode {
    calendar_not_found,
    permission_denied,
    invalid_time_range,
    invalid_timezone,
    invalid_input,
    alias_already_used,
    calendar_closed,
    database_error,
    render_error
};

class DomainError : public std::runtime_error {
  public:
    DomainError(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

  private:
    ErrorCode code_;
};

} // namespace horelac::domain

