// Copyright (C) 2009--2018, 2023, 2025 Jed Brown, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cassert>

#include "pism/rheology/FlowLawFactory.hh"
#include "pism/util/Config.hh"
#include "pism/util/error_handling.hh"

#include "pism/rheology/IsothermalGlen.hh"
#include "pism/rheology/PatersonBudd.hh"
#include "pism/rheology/GPBLD.hh"
#include "pism/rheology/Hooke.hh"
#include "pism/rheology/PatersonBuddCold.hh"
#include "pism/rheology/PatersonBuddWarm.hh"
#include "pism/rheology/GoldsbyKohlstedt.hh"

namespace pism {
namespace rheology {

using ECPtr = std::shared_ptr<EnthalpyConverter>;

FlowLaw *create_isothermal_glen(const std::string &pre, const Config &config, ECPtr EC) {
  return new (IsothermalGlen)(pre, config, EC);
}

FlowLaw *create_pb(const std::string &pre, const Config &config, ECPtr EC) {
  return new (PatersonBudd)(pre, config, EC);
}

FlowLaw *create_gpbld(const std::string &pre, const Config &config, ECPtr EC) {
  return new (GPBLD)(pre, config, EC);
}

FlowLaw *create_hooke(const std::string &pre, const Config &config, ECPtr EC) {
  return new (Hooke)(pre, config, EC);
}

FlowLaw *create_arr(const std::string &pre, const Config &config, ECPtr EC) {
  return new (PatersonBuddCold)(pre, config, EC);
}

FlowLaw *create_arrwarm(const std::string &pre, const Config &config, ECPtr EC) {
  return new (PatersonBuddWarm)(pre, config, EC);
}

FlowLaw *create_goldsby_kohlstedt(const std::string &pre, const Config &config, ECPtr EC) {
  return new (GoldsbyKohlstedt)(pre, config, EC);
}

FlowLawFactory::FlowLawFactory(const std::string &prefix,
                               std::shared_ptr<const Config> conf,
                               std::shared_ptr<EnthalpyConverter> EC)
  : m_config(conf), m_EC(EC) {

  m_prefix = prefix;

  assert(not prefix.empty());

  m_flow_laws.clear();
  add(ICE_ISOTHERMAL_GLEN, &create_isothermal_glen);
  add(ICE_PB, &create_pb);
  add(ICE_GPBLD, &create_gpbld);
  add(ICE_HOOKE, &create_hooke);
  add(ICE_ARR, &create_arr);
  add(ICE_ARRWARM, &create_arrwarm);
  add(ICE_GOLDSBY_KOHLSTEDT, &create_goldsby_kohlstedt);

  set_default(m_config->get_string(prefix + "flow_law"));
}

void FlowLawFactory::add(const std::string &name, FlowLawCreator icreate) {
  m_flow_laws[name] = icreate;
}

void FlowLawFactory::remove(const std::string &name) {
  m_flow_laws.erase(name);
}

void FlowLawFactory::set_default(const std::string &type) {
  if (m_flow_laws[type] == NULL) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Selected ice flow law \"%s\" is not available"
                                  " (prefix=\"%s\").",
                                  type.c_str(), m_prefix.c_str());
  }

  m_type_name = type;
}

std::shared_ptr<FlowLaw> FlowLawFactory::create() {
  // find the function that can create selected flow law:
  auto r = m_flow_laws[m_type_name];
  if (r == NULL) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Selected ice flow law \"%s\" is not available,\n"
                                  "but we shouldn't be able to get here anyway",
                                  m_type_name.c_str());
  }

  // create an FlowLaw instance:
  return std::shared_ptr<FlowLaw>((*r)(m_prefix, *m_config, m_EC));
}

} // end of namespace rheology
} // end of namespace pism
