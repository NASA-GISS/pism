/* Copyright (C) 2015--2023 PISM Authors
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

#include "pism/coupler/ocean/Factory.hh"

// ocean models:
#include "pism/coupler/ocean/Anomaly.hh"
#include "pism/coupler/ocean/Constant.hh"
#include "pism/coupler/ocean/ConstantPIK.hh"
#include "pism/coupler/ocean/GivenClimate.hh"
#include "pism/coupler/ocean/Delta_T.hh"
#include "pism/coupler/ocean/Delta_SMB.hh"
#include "pism/coupler/ocean/Delta_MBP.hh"
#include "pism/coupler/ocean/Frac_MBP.hh"
#include "pism/coupler/ocean/Frac_SMB.hh"
#include "pism/coupler/ocean/ISMIP6.hh"
#include "pism/coupler/ocean/ISMIP6_NL.hh"
#include "pism/coupler/ocean/Runoff_SMB.hh"
#include "pism/coupler/ocean/Cache.hh"
#include "pism/coupler/ocean/GivenTH.hh"
#include "pism/coupler/ocean/Pico.hh"

namespace pism {
namespace ocean {
// Ocean
Factory::Factory(std::shared_ptr<const Grid> g)
  : PCFactory<OceanModel>(g, "ocean.models") {

  add_model<GivenTH>("th");
  add_model<PIK>("pik");
  add_model<Constant>("constant");
  add_model<Pico>("pico");
  add_model<Given>("given");
  add_model<ISMIP6>("ismip6");
  add_model<ISMIP6nl>("ismip6nl");

  add_modifier<Anomaly>("anomaly");
  add_modifier<Cache>("cache");
  add_modifier<Delta_SMB>("delta_SMB");
  add_modifier<Frac_SMB>("frac_SMB");
  add_modifier<Delta_T>("delta_T");
  add_modifier<Runoff_SMB>("runoff_SMB");
  add_modifier<Delta_MBP>("delta_MBP");
  add_modifier<Frac_MBP>("frac_MBP");
}

} // end of namespace ocean
} // end of namespace pism
