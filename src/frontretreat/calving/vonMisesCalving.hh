/* Copyright (C) 2016, 2017, 2018, 2019, 2022, 2023 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef VONMISESCALVING_H
#define VONMISESCALVING_H

#include "pism/frontretreat/calving/StressCalving.hh"

namespace pism {

class Geometry;

namespace rheology {
class FlowLaw;
} // end of namespace rheology

namespace calving {

class vonMisesCalving : public StressCalving {
public:
  vonMisesCalving(std::shared_ptr<const Grid> grid, std::shared_ptr<const rheology::FlowLaw> flow_law);
  virtual ~vonMisesCalving() = default;

  void init();

  void update(const array::CellType1 &cell_type,
              const array::Scalar &ice_thickness,
              const array::Vector1 &ice_velocity,
              const array::Array3D &ice_enthalpy);
  const array::Scalar& threshold() const;

protected:
  DiagnosticList diagnostics_impl() const;

  array::Scalar m_calving_threshold;

  std::shared_ptr<const rheology::FlowLaw> m_flow_law;
};

} // end of namespace calving
} // end of namespace pism

#endif /* VONMISESCALVING_H */
