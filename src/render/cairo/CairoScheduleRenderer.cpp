#include "horelac/render/ScheduleRenderer.hpp"
#include "horelac/domain/Errors.hpp"
#include <cairo/cairo.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
namespace horelac::render {
namespace {
using Surface=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
using Context=std::unique_ptr<cairo_t,decltype(&cairo_destroy)>;
cairo_status_t write_png(void* closure,const unsigned char* data,unsigned int length){auto& bytes=*static_cast<std::vector<std::uint8_t>*>(closure);bytes.insert(bytes.end(),data,data+length);return CAIRO_STATUS_SUCCESS;}
void text(cairo_t* cr,double x,double y,double size,const std::string& value,bool bold=false){cairo_select_font_face(cr,"Sans",CAIRO_FONT_SLANT_NORMAL,bold?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,size);cairo_move_to(cr,x,y);cairo_show_text(cr,value.c_str());}
std::string time_label(int minute){return std::format("{:02}:{:02}",minute/60,minute%60);}
}
RenderResult CairoScheduleRenderer::render_weekly(const domain::Calendar& calendar,const domain::WeeklyHeatmap& heatmap,std::size_t week_index,std::size_t week_count){
    constexpr int width=1400; const int slots=(calendar.config.end_minute-calendar.config.start_minute)/calendar.config.slot_minutes; const int height=260+std::max(520,slots*45);
    Surface surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height),cairo_surface_destroy);if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)throw domain::DomainError(domain::ErrorCode::render_error,"Cairo surface failed");Context cr(cairo_create(surface.get()),cairo_destroy);
    cairo_set_source_rgb(cr.get(),0.055,0.071,0.102);cairo_paint(cr.get());cairo_set_source_rgb(cr.get(),0.94,0.96,1.0);text(cr.get(),50,55,30,calendar.title,true);text(cr.get(),50,88,18,std::format("Week {} / {}  |  {}  |  {} participants",week_index+1,week_count,calendar.config.timezone,heatmap.total_participants));
    std::vector<domain::LocalDate> dates;for(auto d=std::chrono::sys_days{heatmap.first_date};d<=std::chrono::sys_days{heatmap.last_date};d+=std::chrono::days{1})dates.emplace_back(d);const double left=135,top=145,right=45,bottom=95;const double cell_w=(width-left-right)/dates.size();const double cell_h=(height-top-bottom)/slots;
    int peak=0;for(const auto& c:heatmap.cells)peak=std::max(peak,c.available_count);static constexpr std::array<const char*,7> names{"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    for(std::size_t d=0;d<dates.size();++d){const auto wd=std::chrono::weekday{std::chrono::sys_days{dates[d]}};const auto index=(wd.iso_encoding()+6)%7;cairo_set_source_rgb(cr.get(),0.8,0.85,0.92);text(cr.get(),left+d*cell_w+12,top-20,18,std::format("{} {:02}",names[index],static_cast<unsigned>(dates[d].day())),true);}
    std::map<std::pair<domain::LocalDate,int>,int> values;for(const auto& c:heatmap.cells)values[{c.date,c.start_minute}]=c.available_count;
    for(int row=0;row<slots;++row){const int minute=calendar.config.start_minute+row*calendar.config.slot_minutes;cairo_set_source_rgb(cr.get(),0.75,0.8,0.87);text(cr.get(),35,top+row*cell_h+cell_h*.68,16,time_label(minute));for(std::size_t col=0;col<dates.size();++col){const int count=values[{dates[col],minute}];const double intensity=peak?static_cast<double>(count)/peak:0.0;const double x=left+col*cell_w,y=top+row*cell_h;cairo_set_source_rgb(cr.get(),0.08+0.08*intensity,0.13+0.55*intensity,0.22+0.68*intensity);cairo_rectangle(cr.get(),x+1,y+1,cell_w-2,cell_h-2);cairo_fill(cr.get());if(calendar.config.cell_value_mode!=domain::CellValueMode::none&&count>0){cairo_set_source_rgb(cr.get(),1,1,1);std::string value=std::to_string(count);if(calendar.config.cell_value_mode==domain::CellValueMode::fraction)value=std::format("{}/{}",count,heatmap.total_participants);else if(calendar.config.cell_value_mode==domain::CellValueMode::percentage)value=std::format("{:.0f}%",100.0*count/std::max(1,heatmap.total_participants));text(cr.get(),x+cell_w*.38,y+cell_h*.68,15,value,true);}}}
    cairo_set_source_rgb(cr.get(),0.78,0.83,0.9);text(cr.get(),left,height-45,16,std::format("Legend: low  ->  high (weekly peak: {})",peak));RenderResult result{{},width,height};if(cairo_surface_write_to_png_stream(surface.get(),write_png,&result.png)!=CAIRO_STATUS_SUCCESS)throw domain::DomainError(domain::ErrorCode::render_error,"PNG encoding failed");return result;
}
} // namespace horelac::render

