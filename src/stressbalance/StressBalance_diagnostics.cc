// Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2022, 2023, 2024, 2025 Constantine Khroulev
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

#include <cmath>                // std::pow, std::sqrt
#include <algorithm>            // std::max


#include "pism/stressbalance/StressBalance_diagnostics.hh"
#include "pism/stressbalance/SSB_Modifier.hh"
#include "pism/stressbalance/ShallowStressBalance.hh"
#include "pism/util/Mask.hh"
#include "pism/util/Config.hh"
#include "pism/util/Vars.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/pism_utilities.hh"
#include "pism/util/array/CellType.hh"
#include "pism/rheology/FlowLaw.hh"
#include "pism/rheology/FlowLawFactory.hh"
#include "pism/util/Context.hh"

namespace pism {
namespace stressbalance {

DiagnosticList StressBalance::diagnostics_impl() const {
  DiagnosticList result = {
    {"bfrict",              Diagnostic::Ptr(new PSB_bfrict(this))},
    {"velbar_mag",          Diagnostic::Ptr(new PSB_velbar_mag(this))},
    {"flux",                Diagnostic::Ptr(new PSB_flux(this))},
    {"flux_mag",            Diagnostic::Ptr(new PSB_flux_mag(this))},
    {"velbase_mag",         Diagnostic::Ptr(new PSB_velbase_mag(this))},
    {"velsurf_mag",         Diagnostic::Ptr(new PSB_velsurf_mag(this))},
    {"uvel",                Diagnostic::Ptr(new PSB_uvel(this))},
    {"vvel",                Diagnostic::Ptr(new PSB_vvel(this))},
    {"strainheat",          Diagnostic::Ptr(new PSB_strainheat(this))},
    {"velbar",              Diagnostic::Ptr(new PSB_velbar(this))},
    {"velbase",             Diagnostic::Ptr(new PSB_velbase(this))},
    {"velsurf",             Diagnostic::Ptr(new PSB_velsurf(this))},
    {"wvel",                Diagnostic::Ptr(new PSB_wvel(this))},
    {"wvelbase",            Diagnostic::Ptr(new PSB_wvelbase(this))},
    {"wvelsurf",            Diagnostic::Ptr(new PSB_wvelsurf(this))},
    {"wvel_rel",            Diagnostic::Ptr(new PSB_wvel_rel(this))},
    {"strain_rates",        Diagnostic::Ptr(new PSB_strain_rates(this))},
    {"vonmises_stress",     Diagnostic::Ptr(new PSB_vonmises_stress(this))},
    {"deviatoric_stresses", Diagnostic::Ptr(new PSB_deviatoric_stresses(this))},
    {"pressure",            Diagnostic::Ptr(new PSB_pressure(this))},
    {"tauxz",               Diagnostic::Ptr(new PSB_tauxz(this))},
    {"tauyz",               Diagnostic::Ptr(new PSB_tauyz(this))}
  };

  if (m_config->get_flag("output.ISMIP6")) {
    result["velmean"] = Diagnostic::Ptr(new PSB_velbar(this));
    result["zvelbase"] = Diagnostic::Ptr(new PSB_wvelbase(this));
    result["zvelsurf"] = Diagnostic::Ptr(new PSB_wvelsurf(this));
  }

  // add diagnostics from the shallow stress balance and the "modifier"
  result = pism::combine(result, m_shallow_stress_balance->diagnostics());
  result = pism::combine(result, m_modifier->diagnostics());

  return result;
}

TSDiagnosticList StressBalance::ts_diagnostics_impl() const {
  return pism::combine(m_shallow_stress_balance->ts_diagnostics(),
                       m_modifier->ts_diagnostics());
}

PSB_velbar::PSB_velbar(const StressBalance *m)
  : Diag<StressBalance>(m) {

  auto ismip6 = m_config->get_flag("output.ISMIP6");

  // set metadata:
  m_vars = {{m_sys, ismip6 ? "xvelmean" : "ubar"},
            {m_sys, ismip6 ? "yvelmean" : "vbar"}};

  m_vars[0]
      .long_name("vertical mean of horizontal ice velocity in the X direction")
      .standard_name("land_ice_vertical_mean_x_velocity")
      .units("m s^-1")
      .output_units("m year^-1");
  m_vars[1]
      .long_name("vertical mean of horizontal ice velocity in the Y direction")
      .standard_name("land_ice_vertical_mean_y_velocity")
      .units("m s^-1")
      .output_units("m year^-1");
}

std::shared_ptr<array::Array> PSB_velbar::compute_impl() const {
  // get the thickness
  const array::Scalar* thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  // Compute the vertically-integrated horizontal ice flux:
  auto result = array::cast<array::Vector>(PSB_flux(model).compute());

  // Override metadata set by the flux computation
  result->metadata(0) = m_vars[0];
  result->metadata(1) = m_vars[1];

  array::AccessScope list{thickness, result.get()};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();
    double thk = (*thickness)(i,j);

    // Ice flux is masked already, but we need to check for division
    // by zero anyway.
    if (thk > 0.0) {
      (*result)(i,j) /= thk;
    } else {
      (*result)(i,j) = 0.0;
    }
  }

  return result;
}

PSB_velbar_mag::PSB_velbar_mag(const StressBalance *m)
  : Diag<StressBalance>(m) {

  // set metadata:
  m_vars = {{m_sys, "velbar_mag"}};

  m_vars[0]
      .long_name("magnitude of vertically-integrated horizontal velocity of ice")
      .units("m second^-1")
      .output_units("m year^-1");

  m_vars[0]["_FillValue"] = {to_internal(m_fill_value)};
  m_vars[0]["valid_min"] = {0.0};
}

std::shared_ptr<array::Array> PSB_velbar_mag::compute_impl() const {
  auto result = allocate<array::Scalar>("velbar_mag");

  // compute vertically-averaged horizontal velocity:
  auto velbar = array::cast<array::Vector>(PSB_velbar(model).compute());

  // compute its magnitude:
  compute_magnitude(*velbar, *result);

  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  // mask out ice-free areas:
  apply_mask(*thickness, to_internal(m_fill_value), *result);

  return result;
}


PSB_flux::PSB_flux(const StressBalance *m)
  : Diag<StressBalance>(m) {

  // set metadata:
  m_vars = {{m_sys, "uflux"}, {m_sys, "vflux"}};
  m_vars[0]
      .long_name("Vertically integrated horizontal flux of ice in the X direction")
      .units("m^2 s^-1")
      .output_units("m^2 year^-1");
  m_vars[1]
      .long_name("Vertically integrated horizontal flux of ice in the Y direction")
      .units("m^2 s^-1")
      .output_units("m^2 year^-1");
}

std::shared_ptr<array::Array> PSB_flux::compute_impl() const {
  double H_threshold = m_config->get_number("geometry.ice_free_thickness_standard");

  auto result = allocate<array::Vector>("flux");

  // get the thickness
  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  const array::Array3D
    &u3 = model->velocity_u(),
    &v3 = model->velocity_v();

  array::AccessScope list{&u3, &v3, thickness, result.get()};

  const auto &z = m_grid->z();

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      double H = (*thickness)(i,j);

      // an "ice-free" cell:
      if (H < H_threshold) {
        (*result)(i, j) = 0.0;
        continue;
      }

      // an icy cell:
      {
        auto u = u3.get_column(i, j);
        auto v = v3.get_column(i, j);

        Vector2d Q(0.0, 0.0);

        // ks is "k just below the surface"
        int ks = m_grid->kBelowHeight(H);

        if (ks > 0) {
          Vector2d v0(u[0], v[0]);

          for (int k = 1; k <= ks; ++k) {
            Vector2d v1(u[k], v[k]);

            // trapezoid rule
            Q += (z[k] - z[k - 1]) * 0.5 * (v0 + v1);

            v0 = v1;
          }
        }

        // rectangle method to integrate over the last level
        Q += (H - z[ks]) * Vector2d(u[ks], v[ks]);

        (*result)(i, j) = Q;
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  return result;
}


PSB_flux_mag::PSB_flux_mag(const StressBalance *m)
  : Diag<StressBalance>(m) {

  // set metadata:
  m_vars = { { m_sys, "flux_mag" } };
  m_vars[0]
      .long_name("magnitude of vertically-integrated horizontal flux of ice")
      .units("m^2 s^-1")
      .output_units("m^2 year^-1");
  m_vars[0]["_FillValue"] = {to_internal(m_fill_value)};
  m_vars[0]["valid_min"] = {0.0};
}

std::shared_ptr<array::Array> PSB_flux_mag::compute_impl() const {
  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  // Compute the vertically-averaged horizontal ice velocity:
  auto result = array::cast<array::Scalar>(PSB_velbar_mag(model).compute());

  result->metadata() = m_vars[0];

  array::AccessScope list{thickness, result.get()};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    (*result)(i,j) *= (*thickness)(i,j);
  }

  apply_mask(*thickness, to_internal(m_fill_value), *result);

  return result;
}

PSB_velbase_mag::PSB_velbase_mag(const StressBalance *m)
  : Diag<StressBalance>(m) {

  m_vars = { { m_sys, "velbase_mag" } };
  m_vars[0]
      .long_name("magnitude of horizontal velocity of ice at base of ice")
      .units("m s^-1")
      .output_units("m year^-1");
  m_vars[0]["_FillValue"] = {to_internal(m_fill_value)};
  m_vars[0]["valid_min"] = {0.0};
}

std::shared_ptr<array::Array> PSB_velbase_mag::compute_impl() const {
  auto result = allocate<array::Scalar>("velbase_mag");

  compute_magnitude(*array::cast<array::Vector>(PSB_velbase(model).compute()), *result);

  double fill_value = to_internal(m_fill_value);

  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{&mask, result.get()};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    }
  }

  return result;
}

PSB_velsurf_mag::PSB_velsurf_mag(const StressBalance *m)
  : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "velsurf_mag" } };
  m_vars[0]
      .long_name("magnitude of horizontal velocity of ice at ice surface")
      .units("m s^-1")
      .output_units("m year^-1");
  m_vars[0]["_FillValue"] = {to_internal(m_fill_value)};
  m_vars[0]["valid_min"] = {0.0};
}

std::shared_ptr<array::Array> PSB_velsurf_mag::compute_impl() const {
  double fill_value = to_internal(m_fill_value);

  auto result = allocate<array::Scalar>("velsurf_mag");

  compute_magnitude(*array::cast<array::Vector>(PSB_velsurf(model).compute()), *result);

  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{&mask, result.get()};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    }
  }

  return result;
}


PSB_velsurf::PSB_velsurf(const StressBalance *m)
  : Diag<StressBalance>(m) {

  auto ismip6 = m_config->get_flag("output.ISMIP6");
  m_vars = { { m_sys, ismip6 ? "xvelsurf" : "uvelsurf" },
             { m_sys, ismip6 ? "yvelsurf" : "vvelsurf" } };
  m_vars[0]
      .long_name("x-component of the horizontal velocity of ice at ice surface")
      .standard_name("land_ice_surface_x_velocity"); // InitMIP "standard" name
  m_vars[1]
      .long_name("y-component of the horizontal velocity of ice at ice surface")
      .standard_name("land_ice_surface_y_velocity"); // InitMIP "standard" name

  auto large_number = to_internal(1e6);
  for (auto &v : m_vars) {
    v.units("m s^-1").output_units("m year^-1");
    v["valid_range"] = {-large_number, large_number};
    v["_FillValue"] = {to_internal(m_fill_value)};
  }
}

std::shared_ptr<array::Array> PSB_velsurf::compute_impl() const {
  double fill_value = to_internal(m_fill_value);

  auto result = allocate<array::Vector>("surf");

  array::Scalar u_surf(m_grid, "u_surf");
  array::Scalar v_surf(m_grid, "v_surf");

  const array::Array3D
    &u3 = model->velocity_u(),
    &v3 = model->velocity_v();

  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  extract_surface(u3, *thickness, u_surf);
  extract_surface(v3, *thickness, v_surf);

  const auto &cell_type = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{ &cell_type, &u_surf, &v_surf, result.get() };

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (cell_type.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    } else {
      (*result)(i, j) = { u_surf(i, j), v_surf(i, j) };
    }
  }

  return result;
}

PSB_wvel::PSB_wvel(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "wvel", m_grid->z() } };
  m_vars[0]
      .long_name("vertical velocity of ice, relative to geoid")
      .units("m s^-1")
      .output_units("m year^-1");

  auto large_number        = to_internal(1e6);
  m_vars[0]["valid_range"] = { -large_number, large_number };
}

std::shared_ptr<array::Array> PSB_wvel::compute(bool zero_above_ice) const {
  std::shared_ptr<array::Array3D> result3(
      new array::Array3D(m_grid, "wvel", array::WITHOUT_GHOSTS, m_grid->z()));
  result3->metadata() = m_vars[0];

  const array::Scalar *bed, *uplift;
  bed    = m_grid->variables().get_2d_scalar("bedrock_altitude");
  uplift = m_grid->variables().get_2d_scalar("tendency_of_bedrock_altitude");

  const array::Scalar &thickness = *m_grid->variables().get_2d_scalar("land_ice_thickness");
  const auto &mask               = *m_grid->variables().get_2d_cell_type("mask");

  const array::Array3D &u3 = model->velocity_u(), &v3 = model->velocity_v(),
                       &w3 = model->velocity_w();

  array::AccessScope list{ &thickness, &mask, bed, &u3, &v3, &w3, uplift, result3.get() };

  const double ice_density       = m_config->get_number("constants.ice.density"),
               sea_water_density = m_config->get_number("constants.sea_water.density"),
               R                 = ice_density / sea_water_density;

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      const double *u = u3.get_column(i, j), *v = v3.get_column(i, j), *w = w3.get_column(i, j);
      double *result = result3->get_column(i, j);

      int ks = m_grid->kBelowHeight(thickness(i, j));

      // in the ice:
      if (mask.grounded(i, j)) {
        const double bed_dx = diff_x_p(*bed, i, j), bed_dy = diff_y_p(*bed, i, j),
                     uplift_ij = (*uplift)(i, j);
        for (int k = 0; k <= ks; k++) {
          result[k] = w[k] + uplift_ij + u[k] * bed_dx + v[k] * bed_dy;
        }

      } else { // floating
        const double z_sl = R * thickness(i, j), w_sl = w3.interpolate(i, j, z_sl);

        for (int k = 0; k <= ks; k++) {
          result[k] = w[k] - w_sl;
        }
      }

      // above the ice:
      if (zero_above_ice) {
        // set to zeros
        for (unsigned int k = ks + 1; k < m_grid->Mz(); k++) {
          result[k] = 0.0;
        }
      } else {
        // extrapolate using the topmost value
        for (unsigned int k = ks + 1; k < m_grid->Mz(); k++) {
          result[k] = result[ks];
        }
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  return result3;
}

std::shared_ptr<array::Array> PSB_wvel::compute_impl() const {
  return this->compute(true); // fill wvel above the ice with zeros
}

PSB_wvelsurf::PSB_wvelsurf(const StressBalance *m) : Diag<StressBalance>(m) {

  auto ismip6 = m_config->get_flag("output.ISMIP6");

  // set metadata:
  m_vars = { { m_sys, ismip6 ? "zvelsurf" : "wvelsurf" } };

  m_vars[0]
      .long_name("vertical velocity of ice at ice surface, relative to the geoid")
      .standard_name("land_ice_surface_upward_velocity") // InitMIP "standard" name
      .units("m s^-1")
      .output_units("m year^-1");

  auto large_number = to_internal(1e6);
  m_vars[0]["valid_range"] = { -large_number, large_number };
  m_vars[0]["_FillValue"]  = { to_internal(m_fill_value) };
}

std::shared_ptr<array::Array> PSB_wvelsurf::compute_impl() const {
  double fill_value = to_internal(m_fill_value);

  auto result = allocate<array::Scalar>("wvelsurf");

  // here "false" means "don't fill w3 above the ice surface with zeros"
  auto w3 = array::cast<array::Array3D>(PSB_wvel(model).compute(false));

  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  extract_surface(*w3, *thickness, *result);

  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{ &mask, result.get() };

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    }
  }

  return result;
}

PSB_wvelbase::PSB_wvelbase(const StressBalance *m) : Diag<StressBalance>(m) {

  auto ismip6 = m_config->get_flag("output.ISMIP6");

  m_vars = { { m_sys, ismip6 ? "zvelbase" : "wvelbase" } };
  m_vars[0]
      .long_name("vertical velocity of ice at the base of ice, relative to the geoid")
      .standard_name("land_ice_basal_upward_velocity") // InitMIP "standard" name
      .units("m s^-1")
      .output_units("m year^-1");

  auto large_number = to_internal(1e6);

  m_vars[0]["valid_range"] = { -large_number, large_number };
  m_vars[0]["_FillValue"]  = { to_internal(m_fill_value) };
}

std::shared_ptr<array::Array> PSB_wvelbase::compute_impl() const {
  double fill_value = to_internal(m_fill_value);

  auto result = allocate<array::Scalar>("wvelbase");

  // here "false" means "don't fill w3 above the ice surface with zeros"
  auto w3 = array::cast<array::Array3D>(PSB_wvel(model).compute(false));

  extract_surface(*w3, 0.0, *result);

  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{ &mask, result.get() };

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    }
  }

  return result;
}

PSB_velbase::PSB_velbase(const StressBalance *m) : Diag<StressBalance>(m) {

  auto ismip6 = m_config->get_flag("output.ISMIP6");

  // set metadata:
  m_vars = { { m_sys, ismip6 ? "xvelbase" : "uvelbase" },
             { m_sys, ismip6 ? "yvelbase" : "vvelbase" } };
  m_vars[0]
      .long_name("x-component of the horizontal velocity of ice at the base of ice")
      .standard_name("land_ice_basal_x_velocity"); // InitMIP "standard" name
  m_vars[1]
      .long_name("y-component of the horizontal velocity of ice at the base of ice")
      .standard_name("land_ice_basal_y_velocity"); // InitMIP "standard" name

  auto fill_value   = to_internal(m_fill_value);
  auto large_number = to_internal(1e6);

  for (auto &v : m_vars) {
    v.units("m s^-1").output_units("m year^-1");
    v["valid_range"] = { -large_number, large_number };
    v["_FillValue"]  = { fill_value };
  }
}

std::shared_ptr<array::Array> PSB_velbase::compute_impl() const {
  double fill_value = to_internal(m_fill_value);

  auto result = allocate<array::Vector>("base");

  array::Scalar u_base(m_grid, "u_base");
  array::Scalar v_base(m_grid, "v_base");

  const array::Array3D &u3 = model->velocity_u(), &v3 = model->velocity_v();

  extract_surface(u3, 0.0, u_base);
  extract_surface(v3, 0.0, v_base);

  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  array::AccessScope list{ &mask, &u_base, &v_base, result.get() };

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (mask.ice_free(i, j)) {
      (*result)(i, j) = fill_value;
    } else {
      (*result)(i, j) = { u_base(i, j), v_base(i, j) };
    }
  }

  return result;
}


PSB_bfrict::PSB_bfrict(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "bfrict" } };
  m_vars[0].long_name("basal frictional heating").units("W m^-2");
}

std::shared_ptr<array::Array> PSB_bfrict::compute_impl() const {

  auto result = allocate<array::Scalar>("bfrict");

  result->copy_from(model->basal_frictional_heating());

  return result;
}


PSB_uvel::PSB_uvel(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "uvel", m_grid->z() } };
  m_vars[0]
      .long_name("horizontal velocity of ice in the X direction")
      .standard_name("land_ice_x_velocity")
      .units("m s^-1")
      .output_units("m year^-1");
}

/*!
 * Copy F to result and set it to zero above the surface of the ice.
 */
static void zero_above_ice(const array::Array3D &F, const array::Scalar &H,
                           array::Array3D &result) {

  array::AccessScope list{ &F, &H, &result };

  auto grid = result.grid();

  auto Mz = grid->Mz();

  ParallelSection loop(grid->com);
  try {
    for (auto p = grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      int ks = grid->kBelowHeight(H(i, j));

      const double *F_ij = F.get_column(i, j);
      double *F_out_ij   = result.get_column(i, j);

      // in the ice:
      for (int k = 0; k <= ks; k++) {
        F_out_ij[k] = F_ij[k];
      }
      // above the ice:
      for (unsigned int k = ks + 1; k < Mz; k++) {
        F_out_ij[k] = 0.0;
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();
}

std::shared_ptr<array::Array> PSB_uvel::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "uvel", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata() = m_vars[0];

  zero_above_ice(model->velocity_u(), *m_grid->variables().get_2d_scalar("land_ice_thickness"),
                 *result);

  return result;
}

PSB_vvel::PSB_vvel(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "vvel", m_grid->z() } };
  m_vars[0]
      .long_name("horizontal velocity of ice in the Y direction")
      .standard_name("land_ice_y_velocity")
      .units("m s^-1")
      .output_units("m year^-1");
}

std::shared_ptr<array::Array> PSB_vvel::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "vvel", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata() = m_vars[0];

  zero_above_ice(model->velocity_v(), *m_grid->variables().get_2d_scalar("land_ice_thickness"),
                 *result);

  return result;
}

PSB_wvel_rel::PSB_wvel_rel(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "wvel_rel", m_grid->z() } };
  m_vars[0]
      .long_name("vertical velocity of ice, relative to base of ice directly below")
      .units("m s^-1")
      .output_units("m year^-1");
}

std::shared_ptr<array::Array> PSB_wvel_rel::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "wvel_rel", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata() = m_vars[0];

  zero_above_ice(model->velocity_w(), *m_grid->variables().get_2d_scalar("land_ice_thickness"),
                 *result);

  return result;
}


PSB_strainheat::PSB_strainheat(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "strainheat", m_grid->z() } };
  m_vars[0]
      .long_name("rate of strain heating in ice (dissipation heating)")
      .units("W m^-3")
      .output_units("mW m^-3");
}

std::shared_ptr<array::Array> PSB_strainheat::compute_impl() const {
  auto result =
      std::make_shared<array::Array3D>(m_grid, "strainheat", array::WITHOUT_GHOSTS, m_grid->z());

  result->metadata() = m_vars[0];

  result->copy_from(model->volumetric_strain_heating());

  return result;
}

PSB_strain_rates::PSB_strain_rates(const StressBalance *m) : Diag<StressBalance>(m) {

  m_vars = { { m_sys, "eigen1" }, { m_sys, "eigen2" } };
  m_vars[0]
      .long_name("first eigenvalue of the horizontal, vertically-integrated strain rate tensor")
      .units("s^-1");
  m_vars[1]
      .long_name("second eigenvalue of the horizontal, vertically-integrated strain rate tensor")
      .units("s^-1");
}

std::shared_ptr<array::Array> PSB_strain_rates::compute_impl() const {
  auto velbar = array::cast<array::Vector>(PSB_velbar(model).compute());

  auto result = std::make_shared<array::Array2D<PrincipalStrainRates> >(m_grid, "strain_rates",
                                                                        array::WITHOUT_GHOSTS);
  result->metadata(0) = m_vars[0];
  result->metadata(1) = m_vars[1];

  array::Vector1 velbar_with_ghosts(m_grid, "velbar");

  // copy_from communicates ghosts
  velbar_with_ghosts.copy_from(*velbar);

  array::CellType1 cell_type(m_grid, "cell_type");
  {
    const auto &mask = *m_grid->variables().get_2d_cell_type("mask");
    cell_type.copy_from(mask);
  }

  compute_2D_principal_strain_rates(velbar_with_ghosts, cell_type, *result);

  return result;
}

PSB_deviatoric_stresses::PSB_deviatoric_stresses(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "sigma_xx" }, { m_sys, "sigma_yy" }, { m_sys, "sigma_xy" } };

  m_vars[0].long_name("deviatoric stress in X direction").units("Pa");
  m_vars[1].long_name("deviatoric stress in Y direction").units("Pa");
  m_vars[2].long_name("deviatoric shear stress").units("Pa");
}

std::shared_ptr<array::Array> PSB_deviatoric_stresses::compute_impl() const {

  auto result = std::make_shared<array::Array2D<stressbalance::DeviatoricStresses> >(
      m_grid, "deviatoric_stresses", array::WITHOUT_GHOSTS);
  result->metadata(0) = m_vars[0];
  result->metadata(1) = m_vars[1];
  result->metadata(2) = m_vars[2];

  const array::Array3D *enthalpy = m_grid->variables().get_3d_scalar("enthalpy");
  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  array::Scalar hardness(m_grid, "hardness");
  array::Vector1 velocity(m_grid, "velocity");

  averaged_hardness_vec(*model->shallow()->flow_law(), *thickness, *enthalpy, hardness);

  // copy_from updates ghosts
  velocity.copy_from(*array::cast<array::Vector>(PSB_velbar(model).compute()));

  array::CellType1 cell_type(m_grid, "cell_type");
  {
    const auto &mask = *m_grid->variables().get_2d_cell_type("mask");
    cell_type.copy_from(mask);
  }

  stressbalance::compute_2D_stresses(*model->shallow()->flow_law(), velocity, hardness, cell_type,
                                     *result);

  return result;
}

PSB_pressure::PSB_pressure(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "pressure", m_grid->z() } };
  m_vars[0].long_name("pressure in ice (hydrostatic)").units("Pa");
}

std::shared_ptr<array::Array> PSB_pressure::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "pressure", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata(0) = m_vars[0];

  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  array::AccessScope list{ thickness, result.get() };

  const double rg = m_config->get_number("constants.ice.density") *
                    m_config->get_number("constants.standard_gravity");

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      unsigned int ks  = m_grid->kBelowHeight((*thickness)(i, j));
      double *P_out_ij = result->get_column(i, j);
      const double H   = (*thickness)(i, j);
      // within the ice:
      for (unsigned int k = 0; k <= ks; ++k) {
        P_out_ij[k] = rg * (H - m_grid->z(k)); // FIXME: add atmospheric pressure?
      }
      // above the ice:
      for (unsigned int k = ks + 1; k < m_grid->Mz(); ++k) {
        P_out_ij[k] = 0.0; // FIXME: use atmospheric pressure?
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  return result;
}


PSB_tauxz::PSB_tauxz(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = {{m_sys, "tauxz", m_grid->z()}};
  m_vars[0].long_name("shear stress xz component (in shallow ice approximation SIA)").units("Pa");
}


/*!
 * The SIA-applicable shear stress component tauxz computed here is not used
 * by the model.  This implementation intentionally does not use the
 * eta-transformation or special cases at ice margins.
 * CODE DUPLICATION WITH PSB_tauyz
 */
std::shared_ptr<array::Array> PSB_tauxz::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "tauxz", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata() = m_vars[0];

  const array::Scalar *thickness, *surface;

  thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");
  surface   = m_grid->variables().get_2d_scalar("surface_altitude");

  array::AccessScope list{ surface, thickness, result.get() };

  const double rg = m_config->get_number("constants.ice.density") *
                    m_config->get_number("constants.standard_gravity");

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      unsigned int ks      = m_grid->kBelowHeight((*thickness)(i, j));
      double *tauxz_out_ij = result->get_column(i, j);
      const double H = (*thickness)(i, j), dhdx = diff_x_p(*surface, i, j);

      // within the ice:
      for (unsigned int k = 0; k <= ks; ++k) {
        tauxz_out_ij[k] = -rg * (H - m_grid->z(k)) * dhdx;
      }
      // above the ice:
      for (unsigned int k = ks + 1; k < m_grid->Mz(); ++k) {
        tauxz_out_ij[k] = 0.0;
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  return result;
}


PSB_tauyz::PSB_tauyz(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "tauyz", m_grid->z() } };
  m_vars[0].long_name("shear stress yz component (in shallow ice approximation SIA)").units("Pa");
}


/*!
 * The SIA-applicable shear stress component tauyz computed here is not used
 * by the model.  This implementation intentionally does not use the
 * eta-transformation or special cases at ice margins.
 * CODE DUPLICATION WITH PSB_tauxz
 */
std::shared_ptr<array::Array> PSB_tauyz::compute_impl() const {

  std::shared_ptr<array::Array3D> result(
      new array::Array3D(m_grid, "tauyz", array::WITHOUT_GHOSTS, m_grid->z()));
  result->metadata(0) = m_vars[0];

  const array::Scalar *thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");
  const array::Scalar *surface   = m_grid->variables().get_2d_scalar("surface_altitude");

  array::AccessScope list{ surface, thickness, result.get() };

  const double rg = m_config->get_number("constants.ice.density") *
                    m_config->get_number("constants.standard_gravity");

  ParallelSection loop(m_grid->com);
  try {
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      unsigned int ks      = m_grid->kBelowHeight((*thickness)(i, j));
      double *tauyz_out_ij = result->get_column(i, j);
      const double H = (*thickness)(i, j), dhdy = diff_y_p(*surface, i, j);

      // within the ice:
      for (unsigned int k = 0; k <= ks; ++k) {
        tauyz_out_ij[k] = -rg * (H - m_grid->z(k)) * dhdy;
      }
      // above the ice:
      for (unsigned int k = ks + 1; k < m_grid->Mz(); ++k) {
        tauyz_out_ij[k] = 0.0;
      }
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  return result;
}

PSB_vonmises_stress::PSB_vonmises_stress(const StressBalance *m) : Diag<StressBalance>(m) {
  m_vars = { { m_sys, "vonmises_stress" } };
  m_vars[0].long_name("tensile von Mises stress").units("Pascal");
}

std::shared_ptr<array::Array> PSB_vonmises_stress::compute_impl() const {

  using std::max;
  using std::sqrt;
  using std::pow;

  auto result = allocate<array::Scalar>("vonmises_stress");

  array::Scalar &vonmises_stress = *result;

  auto velbar = array::cast<array::Vector>(PSB_velbar(model).compute());
  array::Vector &velocity = *velbar;

  using StrainRates = array::Array2D<PrincipalStrainRates>;
  auto eigen12 = array::cast<StrainRates>(PSB_strain_rates(model).compute());
  const auto &strain_rates = *eigen12;

  const array::Scalar &ice_thickness = *m_grid->variables().get_2d_scalar("land_ice_thickness");
  const array::Array3D *enthalpy = m_grid->variables().get_3d_scalar("enthalpy");
  const auto &mask = *m_grid->variables().get_2d_cell_type("mask");

  std::shared_ptr<const rheology::FlowLaw> flow_law;

  if (m_config->get_flag("calving.vonmises_calving.use_custom_flow_law")) {
    EnthalpyConverter::Ptr EC = m_grid->ctx()->enthalpy_converter();
    rheology::FlowLawFactory factory("calving.vonmises_calving.", m_config, EC);
    flow_law = factory.create();
  } else {
    flow_law = model->shallow()->flow_law();
  }

  const double *z = &m_grid->z()[0];

  double glen_exponent = flow_law->exponent();

  array::AccessScope list{&vonmises_stress, &velocity, &strain_rates, &ice_thickness,
      enthalpy, &mask};

  for (auto pt = m_grid->points(); pt; pt.next()) {
    const int i = pt.i(), j = pt.j();

    if (mask.icy(i, j)) {

      const double       H = ice_thickness(i, j);
      const unsigned int k = m_grid->kBelowHeight(H);

      const double
        *enthalpy_column   = enthalpy->get_column(i, j),
        hardness           = averaged_hardness(*flow_law, H, k, z, enthalpy_column),
        eigen1             = strain_rates(i, j).eigen1,
        eigen2             = strain_rates(i, j).eigen2;

      // [\ref Morlighem2016] equation 6
      const double effective_tensile_strain_rate = sqrt(0.5 * (pow(max(0.0, eigen1), 2) +
                                                               pow(max(0.0, eigen2), 2)));
      // [\ref Morlighem2016] equation 7
      vonmises_stress(i, j) = sqrt(3.0) * hardness * pow(effective_tensile_strain_rate,
                                                         1.0 / glen_exponent);

    } else { // end of "if (mask.icy(i, j))"
      vonmises_stress(i, j) = 0.0;
    }
  }   // end of loop over grid points

  return result;
}

} // end of namespace stressbalance
} // end of namespace pism
