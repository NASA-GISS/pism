// Copyright (C) 2009--2024 Constantine Khroulev and Ed Bueler
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

#include <set>
#include <algorithm>
#include <cmath>
#include <string>

#include "pism/util/VariableMetadata.hh"

#include "pism/util/Logger.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/File.hh"
#include "pism/util/io/IO_Flags.hh"
#include "pism_utilities.hh"

namespace pism {

VariableMetadata::VariableMetadata(const std::string &name, units::System::Ptr system,
                                   unsigned int ndims)
    : m_n_spatial_dims(ndims),
      m_unit_system(std::move(system)),
      m_short_name(name),
      m_time_independent(false),
      m_output_type(io::PISM_NAT) {

  clear();

  // long_name is unset
  // standard_name is unset
  // coordinates is unset

  // valid_min and valid_max are unset
}

/** Get the number of spatial dimensions.
 */
unsigned int VariableMetadata::n_spatial_dimensions() const {
  return m_n_spatial_dims;
}

/** A "time independent" variable will be saved to a NetCDF
    variable which does not depend on the "time" dimension.
 */
VariableMetadata &VariableMetadata::set_time_independent(bool flag) {
  m_time_independent = flag;

  return *this;
}

VariableMetadata &VariableMetadata::set_output_type(io::Type type) {
  m_output_type = type;

  return *this;
}

bool VariableMetadata::get_time_independent() const {
  return m_time_independent;
}

io::Type VariableMetadata::get_output_type() const {
  return m_output_type;
}

units::System::Ptr VariableMetadata::unit_system() const {
  return m_unit_system;
}

//! @brief Check if the range `[min, max]` is a subset of `[valid_min, valid_max]`.
/*! Throws an exception if this check failed.
 */
void VariableMetadata::check_range(const std::string &filename, double min, double max) const {

  auto units_string = get_string("units");
  auto name_string  = get_name();
  const char *units = units_string.c_str(), *name = name_string.c_str(), *file = filename.c_str();
  double eps = 1e-12;

  if (has_attribute("valid_min") and has_attribute("valid_max")) {
    double valid_min = get_number("valid_min");
    double valid_max = get_number("valid_max");
    if ((min < valid_min - eps) or (max > valid_max + eps)) {
      throw RuntimeError::formatted(
          PISM_ERROR_LOCATION,
          "some values of '%s' in '%s' are outside the valid range [%e, %e] (%s).\n"
          "computed min = %e %s, computed max = %e %s",
          name, file, valid_min, valid_max, units, min, units, max, units);
    }
  } else if (has_attribute("valid_min")) {
    double valid_min = get_number("valid_min");
    if (min < valid_min - eps) {
      throw RuntimeError::formatted(
          PISM_ERROR_LOCATION,
          "some values of '%s' in '%s' are less than the valid minimum (%e %s).\n"
          "computed min = %e %s, computed max = %e %s",
          name, file, valid_min, units, min, units, max, units);
    }
  } else if (has_attribute("valid_max")) {
    double valid_max = get_number("valid_max");
    if (max > valid_max + eps) {
      throw RuntimeError::formatted(
          PISM_ERROR_LOCATION,
          "some values of '%s' in '%s' are greater than the valid maximum (%e %s).\n"
          "computed min = %e %s, computed max = %e %s",
          name, file, valid_max, units, min, units, max, units);
    }
  }
}

SpatialVariableMetadata::SpatialVariableMetadata(units::System::Ptr system, const std::string &name,
                                                 const std::vector<double> &zlevels)
    : VariableMetadata(name, system),
      m_x("x", system),
      m_y("y", system),
      m_z("z", system),
      m_zlevels(zlevels) {

  m_x["axis"]          = "X";
  m_x["long_name"]     = "X-coordinate in Cartesian system";
  m_x["standard_name"] = "projection_x_coordinate";
  m_x["units"]         = "m";

  m_y["axis"]          = "Y";
  m_y["long_name"]     = "Y-coordinate in Cartesian system";
  m_y["standard_name"] = "projection_y_coordinate";
  m_y["units"]         = "m";

  if (m_zlevels.size() > 1) {
    m_z.set_name("z"); // default; can be overridden easily
    m_z["axis"]      = "Z";
    m_z["long_name"] = "Z-coordinate in Cartesian system";
    m_z["units"]     = "m";
    m_z["positive"]  = "up";

    m_n_spatial_dims = 3;
  } else {
    z().set_name("").clear();
    m_n_spatial_dims = 2;
  }
}

const std::vector<double> &SpatialVariableMetadata::levels() const {
  return m_zlevels;
}

//! Report the range of a \b global Vec `v`.
void VariableMetadata::report_range(const Logger &log, double min, double max) const {

  // units::Converter constructor will make sure that units are compatible.
  units::Converter c(m_unit_system, get_string("units"), get_string("output_units"));
  min = c(min);
  max = c(max);

  std::string name = get_name();
  std::string spacer(name.size(), ' ');
  std::string info = get_string("long_name");

  std::string units = get_string("output_units");
  std::string range;
  if (min == max) {
    range = pism::printf("constant %9.3f %s", min, units.c_str());
  } else {
    range = pism::printf("min = %9.3f, max = %9.3f %s", min, max, units.c_str());
  }

  log.message(2,
              "  %s / %s\n"
              "  %s \\ %s\n",
              name.c_str(), info.c_str(), spacer.c_str(), range.c_str());
}

VariableMetadata &SpatialVariableMetadata::x() {
  return m_x;
}

VariableMetadata &SpatialVariableMetadata::y() {
  return m_y;
}

VariableMetadata &SpatialVariableMetadata::z() {
  return m_z;
}

const VariableMetadata &SpatialVariableMetadata::x() const {
  return m_x;
}

const VariableMetadata &SpatialVariableMetadata::y() const {
  return m_y;
}

const VariableMetadata &SpatialVariableMetadata::z() const {
  return m_z;
}

//! Checks if an attribute is present. Ignores empty strings, except
//! for the "units" attribute.
bool VariableMetadata::has_attribute(const std::string &name) const {

  auto j = m_strings.find(name);
  if (j != m_strings.end()) {
    if (name != "units" and (j->second).empty()) {
      return false;
    }

    return true;
  }

  return (m_doubles.find(name) != m_doubles.end());
}

bool VariableMetadata::has_attributes() const {
  return not(this->all_strings().empty() and this->all_doubles().empty());
}

VariableMetadata &VariableMetadata::set_name(const std::string &name) {
  m_short_name = name;

  return *this;
}

//! Set a scalar attribute to a single (scalar) value.
VariableMetadata &VariableMetadata::set_number(const std::string &name, double value) {
  return set_numbers(name, {value});
}

//! Set a scalar attribute to a single (scalar) value.
VariableMetadata &VariableMetadata::set_numbers(const std::string &name, const std::vector<double> &values) {
  m_doubles[name] = values;

  return *this;
}

VariableMetadata &VariableMetadata::clear() {
  m_doubles.clear();
  m_strings.clear();

  return *this;
}

std::string VariableMetadata::get_name() const {
  return m_short_name;
}

//! Get a single-valued scalar attribute.
double VariableMetadata::get_number(const std::string &name) const {
  auto j = m_doubles.find(name);
  if (j != m_doubles.end()) {
    return (j->second)[0];
  }

  throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                "variable \"%s\" does not have a double attribute \"%s\"",
                                get_name().c_str(), name.c_str());
}

//! Get an array-of-doubles attribute.
std::vector<double> VariableMetadata::get_numbers(const std::string &name) const {
  auto j = m_doubles.find(name);
  if (j != m_doubles.end()) {
    return j->second;
  }

  return {};
}

const std::map<std::string, std::string> &VariableMetadata::all_strings() const {
  return m_strings;
}

const std::map<std::string, std::vector<double> > &VariableMetadata::all_doubles() const {
  return m_doubles;
}

/*!
 * Set units that may not be supported by UDUNITS.
 *
 * For example: "Pa s^(1/3)" (ice hardness units with Glen exponent n=3).
 */
VariableMetadata &VariableMetadata::set_units_without_validation(const std::string &value) {
  m_strings["units"]        = value;
  m_strings["output_units"] = value;

  return *this;
}

//! Set a string attribute.
VariableMetadata &VariableMetadata::set_string(const std::string &name, const std::string &value) {

  if (name == "units") {
    // create a dummy object to validate the units string
    units::Unit tmp(m_unit_system, value);

    m_strings[name]           = value;
    m_strings["output_units"] = value;
  } else if (name == "output_units") {
    m_strings[name] = value;

    units::Unit internal(m_unit_system, get_string("units"));
    units::Unit output(m_unit_system, value);

    if (not units::are_convertible(internal, output)) {
      throw RuntimeError::formatted(PISM_ERROR_LOCATION, "units \"%s\" and \"%s\" are not compatible",
                                    get_string("units").c_str(), value.c_str());
    }
  } else if (name == "short_name") {
    set_name(name);
  } else {
    m_strings[name] = value;
  }

  return *this;
}

//! Get a string attribute.
/*!
 * Returns an empty string if an attribute is not set.
 */
std::string VariableMetadata::get_string(const std::string &name) const {
  if (name == "short_name") {
     return get_name();
  }

  auto j = m_strings.find(name);
  if (j != m_strings.end()) {
    return j->second;
  }

  return {};
}

void VariableMetadata::report_to_stdout(const Logger &log, int verbosity_threshold) const {

  const auto &strings = this->all_strings();
  const auto &doubles = this->all_doubles();

  // Find the maximum name length so that we can pad output below:
  size_t max_name_length = 0;
  for (const auto &s : strings) {
    max_name_length = std::max(max_name_length, s.first.size());
  }
  for (const auto &d : doubles) {
    max_name_length = std::max(max_name_length, d.first.size());
  }

  // Print text attributes:
  for (const auto &s : strings) {
    std::string name  = s.first;
    std::string value = s.second;
    std::string padding(max_name_length - name.size(), ' ');

    if (value.empty()) {
      continue;
    }

    log.message(verbosity_threshold, "  %s%s = \"%s\"\n",
                name.c_str(), padding.c_str(), value.c_str());
  }

  // Print double attributes:
  for (const auto &d : doubles) {
    std::string name  = d.first;
    std::vector<double> values = d.second;
    std::string padding(max_name_length - name.size(), ' ');

    if (values.empty()) {
      continue;
    }

    const double large = 1.0e7;
    const double small = 1.0e-4;
    if ((std::fabs(values[0]) >= large) || (std::fabs(values[0]) <= small)) {
      // use scientific notation if a number is big or small
      log.message(verbosity_threshold, "  %s%s = %12.3e\n",
                  name.c_str(), padding.c_str(), values[0]);
    } else {
      log.message(verbosity_threshold, "  %s%s = %12.5f\n",
                  name.c_str(), padding.c_str(), values[0]);
    }

  }
}

ConstAttribute::ConstAttribute(const VariableMetadata *var, const std::string &name)
  : m_name(name), m_var(const_cast<VariableMetadata*>(var)) {
}

ConstAttribute::ConstAttribute(ConstAttribute&& a) noexcept
  : m_name(std::move(a.m_name)), m_var(a.m_var) {
  a.m_name.clear();
  a.m_var = nullptr;
}

ConstAttribute::operator std::string() const {
  return m_var->get_string(m_name);
}

ConstAttribute::operator double() const {
  auto values = m_var->get_numbers(m_name);
  if (values.size() == 1) {
    return values[0];
  }
  throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                "%s:%s has more than one value",
                                m_var->get_name().c_str(), m_name.c_str());
}

ConstAttribute::operator std::vector<double> () const {
  return m_var->get_numbers(m_name);
}

void Attribute::operator=(const std::string &value) {
  m_var->set_string(m_name, value);
}
void Attribute::operator=(const std::initializer_list<double> &value) {
  m_var->set_numbers(m_name, value);
}

void Attribute::operator=(const std::vector<double> &value) {
  m_var->set_numbers(m_name, value);
}

} // end of namespace pism
