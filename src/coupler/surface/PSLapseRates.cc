// Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016 PISM Authors
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

#include <gsl/gsl_math.h>

#include "PSLapseRates.hh"
#include "base/util/io/io_helpers.hh"
#include "base/util/pism_utilities.hh"

namespace pism {
namespace surface {

LapseRates::LapseRates(IceGrid::ConstPtr g, SurfaceModel* in)
  : PLapseRates<SurfaceModel,SurfaceModifier>(g, in) {


  m_smb_lapse_rate = 0;
  m_option_prefix = "-surface_lapse_rate";


  //m_climatic_mass_balance.set_string("pism_intent", "diagnostic");
  //m_climatic_mass_balance.set_string("long_name",
  //                "surface mass balance (accumulation/ablation) rate");
  //m_climatic_mass_balance.set_string("standard_name",
  //                "land_ice_surface_specific_mass_balance_flux");
  //m_climatic_mass_balance.set_string("units", "kg m-2 s-1");
  //m_climatic_mass_balance.set_string("glaciological_units", "kg m-2 year-1");

  //m_ice_surface_temp.set_string("pism_intent", "diagnostic");
  //m_ice_surface_temp.set_string("long_name",
  //                            "ice temperature at the ice surface");
  //m_ice_surface_temp.set_string("units", "K");

  m_precip_scale_factor = 0.0;
  do_precip_scale = options::Bool("-precip_scale_factor", "Scale precipitation according to change in surface elevation");


}

LapseRates::~LapseRates() {
  // empty
}

void LapseRates::init_impl() {
  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  m_input_model->init();

  m_log->message(2,
             "  [using temperature and mass balance lapse corrections]\n");

  init_internal();

  m_smb_lapse_rate = options::Real("-smb_lapse_rate",
                                   "Elevation lapse rate for the surface mass balance,"
                                   " in m year-1 per km",
                                   m_smb_lapse_rate);

  m_precip_scale_factor = options::Real("-precip_scale_factor",
                                    "Elevation scale factor for the surface mass balance,"
                                    " in units per km",
                                    m_precip_scale_factor);
  //This is basically temperature lapse rate 8.2 K/Km as in PATemperaturPIK times precipitation scale rate 5%/K )
  if (do_precip_scale){
        m_log->message(2,
             "   ice upper-surface temperature lapse rate: %3.3f K per km\n"
             "   precipitation scale factor: %3.3f per km\n",
             m_temp_lapse_rate, m_precip_scale_factor);
  } else {

    m_log->message(2,
             "   ice upper-surface temperature lapse rate: %3.3f K per km\n"
             "   ice-equivalent surface mass balance lapse rate: %3.3f m year-1 per km\n",
             m_temp_lapse_rate, m_smb_lapse_rate);
  }

  m_temp_lapse_rate = units::convert(m_sys, m_temp_lapse_rate, "K/km", "K/m");

  m_precip_scale_factor = units::convert(m_sys, m_precip_scale_factor, "km-1", "m-1");

  // convert from [m year-1 / km] to [kg m-2 year-1 / km]
  m_smb_lapse_rate *= m_config->get_double("constants.ice.density");
  m_smb_lapse_rate = units::convert(m_sys, m_smb_lapse_rate,
                                    "(kg m-2) year-1 / km", "(kg m-2) second-1 / m");
}


void LapseRates::mass_flux_impl(IceModelVec2S &result) const {
  m_input_model->mass_flux(result);
  //lapse_rate_correction(result, m_smb_lapse_rate);

  if (do_precip_scale) {
    lapse_rate_scale(result, m_precip_scale_factor);
  } else {
    lapse_rate_correction(result, m_smb_lapse_rate);
  }

}

void LapseRates::temperature_impl(IceModelVec2S &result) const {
  m_input_model->temperature(result);
  lapse_rate_correction(result, m_temp_lapse_rate);
}

} // end of namespace surface
} // end of namespace pism
