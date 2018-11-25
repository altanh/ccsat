#ifndef CCSAT_SAT_H
#define CCSAT_SAT_H

#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <ostream>
#include <vector>
#include <string>
#include <stack>
#include <list>
#include <set>

namespace ccsat {

typedef uint32_t var_t;
typedef std::unordered_map<var_t, bool> Model;

struct Lit {
  var_t var;
  bool sign;  // false = positive, true = negative

  inline Lit negate() const {
    return {var, !sign};
  }

  inline bool eval(const Model &m) const {
    return sign ^ m.at(var);
  }

  inline bool operator==(const Lit &other) const {
    return other.var == var && other.sign == sign;
  }
};

struct Clause {
  std::vector<Lit> lits;

  Clause() {}
  Clause(const std::vector<Lit> &lts) : lits(lts) {}

  inline size_t size() const { return lits.size(); }

  inline bool eval(const Model &m) const {
    for (const auto &lit : lits)
      if (lit.eval(m))
        return true;

    return false;
  }
};

struct CNF {
  std::vector<Clause> clauses;

  static CNF fromDIMACS(std::istream &os);

  inline size_t size() const { return clauses.size(); }

  inline bool eval(const Model &m) const {
    for (const auto &clause : clauses)
      if (!clause.eval(m))
        return false;

    return true;
  }
};

class Solver {
 public:
  // returns true if the given CNF SAT instance is satisfiable, false otherwise
  virtual bool solve(const CNF &cnf) = 0;

  // returns the model solving the SAT instance on sat, otherwise undefined
  virtual Model getModel() const = 0;
};

class DPLLSolver : public Solver {
 public:
  bool solve(const CNF &cnf) override;
  Model getModel() const override;

 private:
  struct _ClauseState {
    std::pair<Lit*, Lit*> watched;
    // true if this clause is not sat under the current model, else false
    bool active;

    inline bool empty() const {
      return (watched.first == nullptr) && (watched.second == nullptr);
    }

    inline bool unital() const {
      return (watched.first != nullptr) ^ (watched.second != nullptr);
    }
  };

  // represents the solver state change occuring after a nondeterministic assignment
  struct _SolverDelta {
    // the forced assignments associated with this delta
    std::list<Lit> forced;
    // the principal (nondeterministic) assignment associated with this delta
    Lit principal;

    // PRIOR _ClauseStates that were affected (due to principal & forced assignments)
    // this is used to restore the previous solver state when backtracking
    // size_t indexes into _clause_states
    std::vector<std::pair<size_t, _ClauseState>> priors;

    // safely stores cspair (i.e. does not store if already present, so as to preserve
    // the oldest state)
    void store(const std::pair<size_t, _ClauseState> &cspair);
  };

  // initializes the solver on the given CNF SAT instance
  void _init(const CNF &cnf);
  bool _DPLL();

  // when _deltas nonempty:
  //    - pops _deltas and restores state
  //    - returns true
  // else:
  //    - returns false
  bool _undo();

  // backtracks appropriately w.r.t. the next assignment and returns true, or false if not possible
  bool _backtrack();

  // returns true and outputs an unassigned variable throught out if exists, false otherwise
  bool _chooseVar(var_t *out) const;

  // decides lit to be true and updates the model, deltas, and clause states accordingly
  // nb: this represents a NONDETERMINISTIC assignment, i.e. not forced by previous assignments,
  //     hence it has an associated delta. Forced assignments are directly tied to the delta of
  //     a nondeterministic assignment. (Maybe this should be changed? don't think so.)
  // returns true if no contradiction (i.e. empty clause) was created, else false
  bool _decide(const Lit &lit);

  // returns true if var is assigned in the model, false otherwise
  bool _isAssigned(var_t var) const;

  // finds an unassigned Lit in clause not equal to banned if banned is non-null, else no restriction
  Lit *_findUnassigned(Clause &clause, const Lit *banned) const;

  // propagates lit (might make additional assignments), updates delta, returns true if no contradictions
  // (i.e. empty clauses) were generated, false otherwise.
  // nb: propagation terminates upon encountering any empty clause
  bool _unitPropagate(const Lit &lit, _SolverDelta *delta);
  // assigns pure and does the propagation, updates delta
  void _pureAssign(const Lit &pure, _SolverDelta *delta);

  // finds a unit clause in the current solver state, i.e. an active clause with 1 non-null watched literal.
  // outputs a ptr to the literal in the clause through out and returns true, or returns false if none
  bool _findUnit(Lit *out);
  // finds a pure literal in the current solver state and outputs through out and returns true,
  // or returns false if none
  bool _findPure(Lit *out);

  // completes the model by (arbitrarily) assigning unassigned variables
  void _completeModel();

  // returns true if we have a complete model, false otherwise
  bool _complete() const;

  // returns true if any active clauses are empty (i.e. unsat), false otherwise
  bool _hasEmpty() const;

  // returns true if all clauses are inactive (i.e. sat), false otherwise
  // nb: active != unsat
  bool _allInactive() const;

  // the CNF SAT instance we are working on
  CNF _instance;

  // the current model
  Model _model;

  // the variables in this instance
  std::unordered_set<var_t> _vars;

  // the states of all clauses in the current instance
  // indexing of this mirrors _instance.clauses
  std::vector<_ClauseState> _clause_states;

  std::stack<_SolverDelta> _deltas;
  std::stack<Lit> _assn_stack;

  // x -> [i] s.t. x in C_i for each i in [i] (i indexes _instance.clauses)
  std::unordered_map<var_t, std::vector<size_t>> _pos_map;
  // ... see above but with ~x in C_i
  std::unordered_map<var_t, std::vector<size_t>> _neg_map;
};

}

inline std::ostream &operator<<(std::ostream &os, const ccsat::Lit &lit) {
  os << (lit.sign ? "~" : "") << lit.var;
  return os;
}

inline std::ostream &operator<<(std::ostream &os, const ccsat::Clause &clause) {
  if (clause.size() == 0)
    return (os << "()");

  os << "(" << clause.lits[0];
  for (size_t i = 1; i < clause.lits.size(); ++i)
    os << ", " << clause.lits[i];
  os << ")";

  return os;
}

inline std::ostream &operator<<(std::ostream &os, const ccsat::CNF &cnf) {
  if (cnf.size() == 0)
    return (os << "{}");

  os << "{" << cnf.clauses[0];
  for (size_t i = 1; i < cnf.clauses.size(); ++i)
    os << ", " << cnf.clauses[i];
  os << "}";

  return os;
}

inline std::ostream &operator<<(std::ostream &os, const ccsat::Model &m) {
  std::vector<std::pair<ccsat::var_t, bool>> sorted_pairs;
  for (const auto &pair : m) {
    sorted_pairs.push_back(pair);
  }

  std::sort(sorted_pairs.begin(), sorted_pairs.end(),
      [](const auto &a, const auto &b) {
        return a.first < b.first;
      });

  for (const auto &pair : sorted_pairs) {
    std::cout << (pair.second ? "" : "-") << pair.first << " ";
  }

  return os;
}

#endif