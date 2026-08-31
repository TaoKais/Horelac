#include "horelac/support/Localization.hpp"
namespace horelac::support {
Localization::Localization(std::string fallback) : fallback_(std::move(fallback)) {
    values_["en"]={{"saved","Availability saved successfully."},{"not_found","Calendar not found."},{"denied","You are not allowed to do that."},{"error","The request could not be completed."}};
    values_["es"]={{"saved","Disponibilidad guardada correctamente."},{"not_found","Calendario no encontrado."},{"denied","No tienes permiso para hacer eso."},{"error","No se pudo completar la solicitud."}};
}
std::string Localization::text(std::string_view locale,std::string_view key) const {auto find=[&](std::string_view lang)->const std::string*{auto l=values_.find(std::string(lang));if(l==values_.end())return nullptr;auto v=l->second.find(std::string(key));return v==l->second.end()?nullptr:&v->second;};if(auto* value=find(locale))return *value;if(auto* value=find(fallback_))return *value;return std::string(key);}
} // namespace horelac::support

