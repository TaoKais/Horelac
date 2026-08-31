#pragma once
#include "horelac/domain/Models.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace horelac::render {
struct RenderResult { std::vector<std::uint8_t> png; int width{}; int height{}; };
class IScheduleRenderer {
  public:
    virtual ~IScheduleRenderer() = default;
    virtual RenderResult render_weekly(const domain::Calendar&, const domain::WeeklyHeatmap&, std::size_t week_index, std::size_t week_count) = 0;
};
class CairoScheduleRenderer final : public IScheduleRenderer {
  public:
    RenderResult render_weekly(const domain::Calendar&, const domain::WeeklyHeatmap&, std::size_t week_index, std::size_t week_count) override;
};
} // namespace horelac::render

