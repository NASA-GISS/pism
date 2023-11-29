// Copyright (C) 2010--2023 PISM Authors
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

#ifndef _SSB_MODIFIER_H_
#define _SSB_MODIFIER_H_

#include "pism/util/array/Staggered.hh"
#include "pism/util/Component.hh"
#include "pism/util/EnthalpyConverter.hh"

namespace pism {

namespace array {
class Vector;
}

namespace rheology {
class FlowLaw;
}

namespace stressbalance {

class Inputs;

//! Shallow stress balance modifier (such as the non-sliding SIA).
class SSB_Modifier : public Component {
public:
  SSB_Modifier(std::shared_ptr<const Grid> g);
  virtual ~SSB_Modifier() = default;

  virtual void init();

  virtual void update(const array::Vector &sliding_velocity,
                      const Inputs &inputs,
                      bool full_update) = 0;

  //! \brief Get the diffusive (SIA) vertically-averaged flux on the staggered grid.
  const array::Staggered& diffusive_flux();

  //! \brief Get the max diffusivity (for the adaptive time-stepping).
  double max_diffusivity() const;

  const array::Array3D& velocity_u() const;

  const array::Array3D& velocity_v() const;

  virtual std::string stdout_report() const;

  std::shared_ptr<const rheology::FlowLaw> flow_law() const;

protected:
  std::shared_ptr<rheology::FlowLaw> m_flow_law;
  EnthalpyConverter::Ptr m_EC;
  double m_D_max;
  array::Staggered1 m_diffusive_flux;
  array::Array3D m_u, m_v;
};


//! The trivial Shallow Stress Balance modifier.
class ConstantInColumn : public SSB_Modifier {
public:
  ConstantInColumn(std::shared_ptr<const Grid> g);
  virtual ~ConstantInColumn() = default;

  virtual void init();

  virtual void update(const array::Vector &sliding_velocity,
                      const Inputs &inputs,
                      bool full_update);
};

} // end of namespace stressbalance
} // end of namespace pism

#endif /* _SSB_MODIFIER_H_ */
