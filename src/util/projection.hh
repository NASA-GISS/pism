/* Copyright (C) 2015, 2016, 2017, 2018, 2019, 2020, 2023, 2024 PISM Authors
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

#ifndef PISM_PROJECTION_H
#define PISM_PROJECTION_H

#include <string>
#include <array>

#include "pism/util/Units.hh"
#include "pism/util/VariableMetadata.hh"

namespace pism {

class File;
namespace array {
class Array3D;
class Scalar;
}

/*!
 * Return the string that describes a 2D grid present in a NetCDF file.
 *
 * Here `variable_name` is the name of a 2D variable used to extract
 * grid information.
 *
 * We assume that a file may contain more than one grid, so the file
 * name alone is not sufficient.
 *
 * Appends ":piecewise_constant" if `piecewise_constant` is true.
 *
 * The output has the form "input_file.nc:y:x".
 */
std::string grid_name(const File &file, const std::string &variable_name,
                      units::System::Ptr sys, bool piecewise_constant);

/*! @brief Convert a proj string with an EPSG code to a set of CF attributes. */
/*!
 * Fails if `proj_string` does not contain an EPSG code.
 */
VariableMetadata epsg_to_cf(units::System::Ptr system, const std::string &proj_string);

class MappingInfo {
public:
  MappingInfo(const std::string &mapping_variable_name, units::System::Ptr unit_system);
  MappingInfo(const VariableMetadata &mapping_variable, const std::string &proj_string);

  /*! @brief Get projection info from a file. */
  static MappingInfo FromFile(const File &input_file, const std::string &variable_name,
                              units::System::Ptr unit_system);

  //! grid mapping description following CF conventions
  VariableMetadata cf_mapping;

  //! a projection definition string in a format recognized by PROJ 6.x+
  std::string proj_string;
};

void write_mapping(const File &file, const pism::MappingInfo &info);

/*!
 * Parse a string "EPSG:XXXX", "epsg:XXXX", "+init=epsg:XXXX" and return the EPSG code
 * (XXXX).
 */
int parse_epsg(const std::string &proj_string);

/*! @brief Check consistency of the "mapping" variable with the EPSG code in the proj string. */
/*!
 * If the consistency check fails, throws RuntimeError explaining the failure. Fails if `info.proj`
 * does not use an EPSG code.
 */
void check_consistency_epsg(const VariableMetadata &cf_mapping, const std::string &proj_string);

void compute_longitude(const std::string &projection, array::Scalar &result);
void compute_latitude(const std::string &projection, array::Scalar &result);

void compute_lon_bounds(const std::string &projection, array::Array3D &result);
void compute_lat_bounds(const std::string &projection, array::Array3D &result);

/*!
 * Convert Climate and Forecasting (CF) convention metadata to a PROJ.4 style projection
 * definition.
 */
std::string cf_to_proj(const VariableMetadata &mapping);

} // end of namespace pism

#endif /* PISM_PROJECTION_H */
