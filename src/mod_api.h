#ifndef SNAP_MOD_API_H
#define SNAP_MOD_API_H

namespace snap {

// Registers the functions a mod imports from the port itself, so the
// runtime's mod loader can resolve them: recomp_printf (mod_api.cpp) and the
// collections of recompdata.h (mod_data_api.cpp). Before recomp::start.
void register_mod_exports();
void register_data_api_exports();

} // namespace snap

#endif // SNAP_MOD_API_H
