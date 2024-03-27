/* Copyright (C) 2024 PISM Authors
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

#include <memory>

#include "pism/util/Interpolation2DRegular.hh"
#include "pism/util/Context.hh"
#include "pism/util/Grid.hh"
#include "pism/util/io/io_helpers.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {

Interpolation2DRegular::Interpolation2DRegular(std::shared_ptr<const Grid> target_grid,
                                               const File &input_file,
                                               const std::string &variable_name,
                                               InterpolationType type)
  : m_target_grid(target_grid) {

  auto log = target_grid->ctx()->log();
  auto unit_system = target_grid->ctx()->unit_system();

  grid::InputGridInfo input_grid(input_file, variable_name, unit_system, target_grid->registration());

  input_grid.report(*log, 4, unit_system);

  io::check_input_grid(input_grid, *target_grid, {0.0});

  m_interp_context = std::make_shared<LocalInterpCtx>(input_grid, *target_grid, type);
}


double Interpolation2DRegular::regrid_impl(const pism::File &file,
                                           const SpatialVariableMetadata &metadata,
                                           petsc::Vec &output) const {

  petsc::VecArray output_array(output);


  double start = get_time(m_target_grid->com);
  io::regrid_spatial_variable(metadata, *m_target_grid, *m_interp_context, file, output_array.get());
  double end = get_time(m_target_grid->com);

  return end - start;
}

} // namespace pism
