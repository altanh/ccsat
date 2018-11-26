#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include "SAT.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " bench.cnf [...]" << std::endl;
    return 1;
  }

  for (int i = 1; i < argc; ++i) {
    std::ifstream bench(argv[i]);
    if (!bench.is_open()) {
      std::cerr << "failed to open " << argv[i] << std::endl;
      return 1;
    }

    ccsat::CNF cnf = ccsat::CNF::fromDIMACS(bench);

    bench.close();

    ccsat::Solver *solver = new ccsat::DPLLSolver();
    bool sat = solver->solve(cnf);

    std::cout << (sat ? "sat" : "unsat") << std::endl;
    if (sat) {
      if (cnf.eval(solver->getModel())) {
        std::cout << "model validated" << std::endl;
      } else {
        std::cout << "invalid model" << std::endl;
      }

      std::cout << solver->getModel() << std::endl;
    }

    delete solver;
  }

  return 0;
}