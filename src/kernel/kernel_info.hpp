#pragma once

#include <string>

namespace nfcad::kernel {

/// Version string of the linked Nurbforge kernel, e.g. "0.7.0".
[[nodiscard]] std::string nurbforge_version();

/// Evaluates a Nurbforge circle once – a smoke test that the kernel is linked and works.
[[nodiscard]] bool self_test();

}  // namespace nfcad::kernel
