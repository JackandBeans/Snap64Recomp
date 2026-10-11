#ifndef SNAP_MOD_API_H
#define SNAP_MOD_API_H

#include <string>

namespace snap {

// Registers the functions a mod imports from the port itself, so the
// runtime's mod loader can resolve them: recomp_printf (mod_api.cpp), the
// collections of recompdata.h (mod_data_api.cpp) and the computer's clock
// (mod_time_api.cpp, snap64.h). Before recomp::start.
void register_mod_exports();
void register_data_api_exports();
void register_time_api_exports();

// The line a mod set for its details page (snap64_set_status), or empty.
std::string mod_status_line(const std::string& id);

// The clock a mod reads, as the page writes it: "6:41 PM" (SNAP_CLOCK's
// clock when that is set, so a test sees what the mod sees).
std::string local_time_text();

} // namespace snap

#endif // SNAP_MOD_API_H
