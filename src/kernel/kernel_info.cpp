#include "kernel/kernel_info.hpp"

#include <cmath>
#include <nurbforge/nurbforge.hpp>

namespace nfcad::kernel {

std::string nurbforge_version() { return NURBFORGE_VERSION; }

bool self_test() {
  const nf::NurbsCurve circle = nf::make_circle({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, 2.0);
  const double u = 0.5 * (circle.domain_start() + circle.domain_end());
  return std::abs(nf::norm(circle.evaluate(u)) - 2.0) < 1e-9;
}

}  // namespace nfcad::kernel
