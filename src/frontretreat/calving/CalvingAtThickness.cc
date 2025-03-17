/* Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2021, 2022, 2023, 2024, 2025 PISM Authors
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

#include "pism/frontretreat/calving/CalvingAtThickness.hh"

#include "pism/util/Mask.hh"
#include "pism/util/Grid.hh"
#include "pism/coupler/util/options.hh"
#include "pism/util/array/Forcing.hh"
#include "pism/util/io/File.hh"
#include "pism/util/Logger.hh"

namespace pism {

//! @brief Calving and iceberg removal code.
namespace calving {

CalvingAtThickness::CalvingAtThickness(std::shared_ptr<const Grid> g)
  : Component(g),
    m_old_mask(m_grid, "old_mask") {

  ForcingOptions opt(*m_grid->ctx(), "calving.thickness_calving");
  {
    unsigned int buffer_size = m_config->get_number("input.forcing.buffer_size");

    File file(m_grid->com, opt.filename, io::PISM_NETCDF3, io::PISM_READONLY);

    m_calving_threshold = std::make_shared<array::Forcing>(m_grid,
                                                      file,
                                                      "thickness_calving_threshold",
                                                      "", // no standard name
                                                      buffer_size,
                                                      opt.periodic,
                                                      LINEAR);

    m_calving_threshold->metadata(0)
        .long_name("threshold used by the 'calving at threshold' calving method")
        .units("m"); // no standard name
    m_calving_threshold->metadata()["valid_min"] = {0.0};
  }
}

void CalvingAtThickness::init() {

  m_log->message(2, "* Initializing the 'calving at a threshold thickness' mechanism...\n");

  ForcingOptions opt(*m_grid->ctx(), "calving.thickness_calving");

  File file(m_grid->com, opt.filename, io::PISM_GUESS, io::PISM_READONLY);
  auto variable_exists = file.variable_exists(m_calving_threshold->get_name());
  if (variable_exists) {
    m_log->message(2,
                   "  Reading thickness calving threshold from file '%s'...\n",
                   opt.filename.c_str());

    m_calving_threshold->init(opt.filename, opt.periodic);
  } else {
    double calving_threshold = m_config->get_number("calving.thickness_calving.threshold");

    SpatialVariableMetadata attributes = m_calving_threshold->metadata();
    // replace with a constant array::Forcing
    m_calving_threshold = array::Forcing::Constant(m_grid,
                                                  "thickness_calving_threshold",
                                                  calving_threshold);
    // restore metadata
    m_calving_threshold->metadata() = attributes;

    m_log->message(2,
                   "  Thickness threshold: %3.3f meters.\n", calving_threshold);
  }
}

/**
 * Updates ice cover mask and the ice thickness according to the
 * calving rule removing ice at the shelf front that is thinner than a
 * given threshold.
 *
 * @param[in] t beginning of the time step
 * @param[in] dt length of the time step
 * @param[in,out] pism_mask ice cover mask
 * @param[in,out] ice_thickness ice thickness
 *
 * @return 0 on success
 */
void CalvingAtThickness::update(double t,
                                double dt,
                                array::Scalar &cell_type,
                                array::Scalar &ice_thickness) {

  m_calving_threshold->update(t, dt);
  m_calving_threshold->average(t, dt);

  // this call fills ghosts of m_old_mask
  m_old_mask.copy_from(cell_type);

  const auto &threshold = *m_calving_threshold;

  array::AccessScope list{&cell_type, &ice_thickness, &m_old_mask, &threshold};
  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (m_old_mask.floating_ice(i, j)           &&
        m_old_mask.next_to_ice_free_ocean(i, j) &&
        ice_thickness(i, j) < threshold(i, j)) {
      ice_thickness(i, j) = 0.0;
      cell_type(i, j)     = MASK_ICE_FREE_OCEAN;
    }
  }

  cell_type.update_ghosts();
  ice_thickness.update_ghosts();
}

const array::Scalar& CalvingAtThickness::threshold() const {
  return *m_calving_threshold;
}

DiagnosticList CalvingAtThickness::diagnostics_impl() const {
  return {{"thickness_calving_threshold", Diagnostic::wrap(*m_calving_threshold)}};
}

} // end of namespace calving
} // end of namespace pism
