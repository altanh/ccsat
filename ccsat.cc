#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "SAT.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " bench.cnf" << std::endl;
    return 1;
  }

  std::ifstream bench(argv[1]);
  if (!bench.is_open()) {
    std::cerr << "failed to open " << argv[1] << std::endl;
    return 1;
  }

  ccsat::CNF cnf = ccsat::CNF::fromDIMACS(bench);

  bench.close();

  ccsat::DPLLSolver solver;
  bool sat = solver.solve(cnf);

  std::cout << (sat ? "sat" : "unsat") << std::endl;
  if (sat) {
    std::cout << solver.getModel() << std::endl;

    if (cnf.eval(solver.getModel())) {
      std::cout << "model validated" << std::endl;
    } else {
      std::cout << "invalid model" << std::endl;
    }
  }

  return 0;
}