// Photon - FDS exporter (see fds_export.cpp).
#pragma once
#include "sim/devices.h"
#include <string>
#include <vector>

namespace photon {

bool exportFds(const Solver& s, const std::string& path, const std::string& chid, const std::string& title, float tEnd,
               const std::vector<Device>& devs, std::string& err);

} // namespace photon
