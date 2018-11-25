#include <cassert>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stack>
#include <list>

#include "SAT.h"

namespace ccsat {

bool DPLLSolver::solve(const CNF &cnf) {
  // empty case, trivially sat
  if (cnf.size() == 0)
    return true;

  // contains an empty clause, unsat
  if (std::any_of(cnf.clauses.begin(), cnf.clauses.end(),
      [](const auto &clause) { return clause.size() == 0; }))
    return false;

  _init(cnf);

  return _DPLL();
}

Model DPLLSolver::getModel() const {
  return _model;
}

void DPLLSolver::_init(const CNF &cnf) {
  _instance = cnf;

  // build _vars
  for (const auto &clause : _instance.clauses)
    for (const auto &lit : clause.lits)
      _vars.insert(lit.var);

  // build _clause_states
  for (size_t i = 0; i < _instance.clauses.size(); ++i) {
    std::pair<Lit*, Lit*> watched;

    watched.first = _findUnassigned(_instance.clauses[i], nullptr);
    watched.second = _findUnassigned(_instance.clauses[i], watched.first);

    _clause_states.push_back({watched, true});
  }

  // build _pos_map, _neg_map
  for (var_t var : _vars) {
    _pos_map.insert(std::make_pair(var, std::vector<size_t>()));
    _neg_map.insert(std::make_pair(var, std::vector<size_t>()));

    for (size_t i = 0; i < _instance.clauses.size(); ++i) {
      for (const auto &lit : _instance.clauses[i].lits) {
        if (lit.var == var) {
          if (lit.sign && (std::find(_neg_map[var].begin(), _neg_map[var].end(), i)
              == _neg_map[var].end())) {
            _neg_map[var].push_back(i);
          } else if (std::find(_pos_map[var].begin(), _pos_map[var].end(), i)
              == _pos_map[var].end()) {
            _pos_map[var].push_back(i);
          }
        }
      }
    }
  }

  // push root decisions
  var_t initial_var;
  _chooseVar(&initial_var);

  _assn_stack.push({initial_var, true});
  _assn_stack.push({initial_var, false});
}

bool DPLLSolver::_DPLL() {
  while (!_assn_stack.empty()) {
    // make an assignment
    _assign(_assn_stack.top());
    _assn_stack.pop();

    // every clause is satisfied, we're done (and have a possibly partial model)
    if (_allInactive()) {
      _completeModel();

      return true;
    }

    if (_hasEmpty()) {
      if (!_backtrack()) {
        return false;
      }

      continue;
    }

    if (_complete()) {
      if (_instance.eval(_model))
        return true;

      if (!_backtrack())
        return false;

      continue;
    }

    // choose a variable and push its possible assignments
    // question: any benefit of choosing a literal instead?
    var_t var;
    if (!_chooseVar(&var)) {
      // this should never happen
      return false;
    }

    assert(_model.count(var) == 0);

    _assn_stack.push({var, true});
    _assn_stack.push({var, false});
  }

  return false;
}

bool DPLLSolver::_undo() {
  if (_deltas.empty()) return false;

  _SolverDelta delta = _deltas.top();
  _deltas.pop();

  // undo assignments
  _model.erase(delta.principal.var);
  assert(_model.count(delta.principal.var) == 0);

  for (const auto &lit : delta.forced) {
    _model.erase(lit.var);
    assert(_model.count(lit.var) == 0);
  }

  // restore clause states
  for (const auto &cspair : delta.priors) {
    _clause_states[cspair.first] = cspair.second;
  }

  return true;
}

bool DPLLSolver::_backtrack() {
  if (_deltas.empty() || _assn_stack.empty()) return false;

  // undo until we reach the matching delta
  while (!(_deltas.top().principal == _assn_stack.top().negate())) {
    if (!_undo())
      return false;
  }

  // then undo that as well
  if (!_undo())
    return false;

  return true;
}

void DPLLSolver::_assign(const Lit &lit) {
  _deltas.emplace();
  _SolverDelta &delta = _deltas.top();

  delta.principal = lit;
  assert(_model.count(lit.var) == 0);
  _model[lit.var] = !lit.sign;

  _unitPropagate(lit, &delta);

  if (_hasEmpty()) return;

  Lit unit;
  while (_findUnit(&unit)) {
    delta.forced.push_back(unit);
    assert(_model.count(unit.var) == 0);
    _model[unit.var] = !unit.sign;
    _unitPropagate(unit, &delta);

    if (_hasEmpty()) return;
  }

  Lit pure;
  while (_findPure(&pure)) {
    delta.forced.push_back(pure);
    assert(_model.count(pure.var) == 0);
    _model[pure.var] = !pure.sign;
    _pureAssign(pure, &delta);
  }
}

void DPLLSolver::_unitPropagate(const Lit &lit, _SolverDelta *delta) {
  // indices of clauses that contain lit and ~lit
  const std::vector<size_t> &pos_indices =
      lit.sign ? _neg_map[lit.var] : _pos_map[lit.var];
  const std::vector<size_t> &neg_indices =
      lit.sign ? _pos_map[lit.var] : _neg_map[lit.var];

  for (auto i : pos_indices) {
    if (_clause_states[i].active) {
      delta->store(std::make_pair(i, _clause_states[i]));

      // mark inactive, satisfied under the model now
      _clause_states[i].active = false;
    }
  }

  Lit negated = lit.negate();
  for (auto i : neg_indices) {
    delta->store(std::make_pair(i, _clause_states[i]));

    // update the watchlist
    if (_clause_states[i].watched.first != nullptr
        && *_clause_states[i].watched.first == negated) {
      // find a unique unassigned literal to watch (might not exist)
      _clause_states[i].watched.first = 
          _findUnassigned(_instance.clauses[i], _clause_states[i].watched.second);
    } else if (_clause_states[i].watched.second != nullptr && 
        *_clause_states[i].watched.second == negated) {
      _clause_states[i].watched.second =
          _findUnassigned(_instance.clauses[i], _clause_states[i].watched.first);
    }
  }
}

void DPLLSolver::_pureAssign(const Lit &pure, _SolverDelta *delta) {
  // pure is guaranteed to be pure in the current active clauses
  const std::vector<size_t> &indices =
    pure.sign ? _neg_map[pure.var] : _pos_map[pure.var];

  for (auto i : indices) {
    if (_clause_states[i].active) {
      delta->store(std::make_pair(i, _clause_states[i]));

      _clause_states[i].active = false;
    }
  }
}

bool DPLLSolver::_findUnit(Lit *out) {
  for (auto &cstate : _clause_states) {
    if (cstate.active) {
      if (cstate.watched.first == nullptr && cstate.watched.second != nullptr) {
        *out = *cstate.watched.second;
        return true;
      }

      if (cstate.watched.first != nullptr && cstate.watched.second == nullptr) {
        *out = *cstate.watched.first;
        return true;
      }
    }
  }

  return false;
}

// this method is annoying, might have bad runtime
//   - possible improvement: keep a list of unassigned variables after each assignment
bool DPLLSolver::_findPure(Lit *out) {
  for (auto var : _vars) {
    if (!_isAssigned(var)) {
      bool found = false;
      bool pol;

      for (size_t pos_index : _pos_map[var]) {
        if (_clause_states[pos_index].active) {
          found = true;
          pol = false;

          break;
        }
      }

      for (size_t neg_index : _neg_map[var]) {
        if (_clause_states[neg_index].active) {
          if (found) {
            // bad, not pure
            found = false;
            break;
          }

          // purely negative
          found = true;
          pol = true;

          break;
        }
      }

      if (found) {
        *out = {var, pol};
        return true;
      }
    }
  }

  return false;
}

// a Clause is empty iff it is active but has no watched literals.
// this checks out, since watched literals are only updated when forced
// falsities occur.
bool DPLLSolver::_hasEmpty() const {
  for (const auto &cs : _clause_states)
    if (cs.active && cs.watched.first == nullptr && cs.watched.second == nullptr)
      return true;

  return false;
}

void DPLLSolver::_completeModel() {
  for (var_t var : _vars)
    if (!_isAssigned(var))
      _model[var] = false;
}

bool DPLLSolver::_complete() const {
  for (auto var : _vars)
    if (!_isAssigned(var))
      return false;

  return true;
}

bool DPLLSolver::_allInactive() const {
  for (const auto &cstate : _clause_states)
    if (cstate.active)
      return false;

  return true;
}

bool DPLLSolver::_isAssigned(var_t var) const {
  return _model.count(var) == 1;
}

Lit *DPLLSolver::_findUnassigned(Clause &clause, const Lit *banned) const {
  for (auto &lit : clause.lits) {
    if (!_isAssigned(lit.var)) {
      if (banned == nullptr || !(lit == *banned))
        return &lit;
    }
  }

  return nullptr;
}

bool DPLLSolver::_chooseVar(var_t *out) const {
  for (auto var : _vars) {
    if (!_isAssigned(var)) {
      *out = var;
      return true;
    }
  }

  return false;
}

void DPLLSolver::_SolverDelta::store(const std::pair<size_t, _ClauseState> &cspair) {
  // we don't want to have multiple prior states, only the oldest one, since the 'newer'
  // states are actually forced from the initial assignment
  if (!std::any_of(priors.begin(), priors.end(), 
      [&cspair](const auto &state) {
        return state.first == cspair.first;
      })) {
    // store the state
    priors.push_back(cspair);
  }
}

CNF CNF::fromDIMACS(std::istream &os) {
  ccsat::CNF cnf;

  std::string line;
  while (std::getline(os, line)) {
    if (line.empty())
      continue;

    if (line[0] == 'c' || line[0] == 'p') {
      std::cout << line << std::endl;
      continue;
    }

    std::stringstream ss(line);
    int val;
    ccsat::Clause clause;
    while (ss >> val) {
      if (val == 0)
        break;

      bool sign = val < 0;
      clause.lits.push_back({static_cast<uint32_t>(std::abs(val)), sign});
    }
    cnf.clauses.push_back(clause);
  }

  return cnf;
}

}
