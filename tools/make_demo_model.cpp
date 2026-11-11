// Writes a STEP demo model for display tests and the --benchmark run (docs/plan.md, M2):
// a grid of solids – plates with cylindrical holes (trimmed faces with holes), spheres, tori,
// cones and cylinders. Usage: make_demo_model out.step [nx ny]
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <nurbforge/nurbforge.hpp>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s out.step [nx ny]\n", argv[0]);
    return 2;
  }
  const int nx = argc > 3 ? std::atoi(argv[2]) : 12;
  const int ny = argc > 3 ? std::atoi(argv[3]) : 12;
  try {
    std::vector<nf::Shell> shells;
    std::vector<std::string> names;
    std::size_t faces = 0;
    for (int i = 0; i < nx; ++i) {
      for (int j = 0; j < ny; ++j) {
        const double x = 30.0 * i;
        const double y = 30.0 * j;
        nf::Shell s = [&]() -> nf::Shell {
          switch ((i + 2 * j) % 5) {
            case 0: {  // plate with a hole
              const nf::Shell box = nf::make_box({x - 10, y - 10, 0}, {20, 20, 8});
              const nf::Shell cyl = nf::make_cylinder({x, y, -1}, {0, 0, 1}, 5.0, 10.0);
              return nf::boolean(box, cyl, nf::BooleanOp::Difference);
            }
            case 1: return nf::make_sphere({x, y, 10}, 9.0);
            case 2: return nf::make_torus({x, y, 4}, {0, 0, 1}, 9.0, 3.0);
            case 3: return nf::make_cone({x, y, 0}, {0, 0, 1}, 9.0, 3.0, 18.0);
            default: return nf::make_cylinder({x, y, 0}, {0, 0, 1}, 8.0, 14.0);
          }
        }();
        faces += s.faces().size();
        names.push_back("part_" + std::to_string(i) + "_" + std::to_string(j));
        shells.push_back(std::move(s));
      }
    }
    nf::StepOptions options;
    options.name = "nurbforge-cad demo";
    options.shell_names = names;
    nf::write_step(argv[1], shells, options);
    std::printf("%s: %zu shells, %zu faces\n", argv[1], shells.size(), faces);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
