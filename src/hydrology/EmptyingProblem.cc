/* Copyright (C) 2019, 2020, 2022, 2023 PISM Authors
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

#include "pism/hydrology/EmptyingProblem.hh"

#include "pism/geometry/Geometry.hh"
#include "pism/util/interpolation.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {
namespace hydrology {

namespace diagnostics {

/*! Compute the map of sinks.
 *
 * This field is purely diagnostic: it can be used to diagnose failures of the code
 * filling dips to eliminate sinks (and get a better estimate of the steady state flow
 * direction).
 */
static void compute_sinks(const array::Scalar &domain_mask, const array::Scalar1 &psi,
                          array::Scalar &result) {

  auto grid = result.grid();

  array::AccessScope list{ &psi, &domain_mask, &result };

  for (auto p = grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    auto P = psi.star(i, j);

    double
      v_n = - (P.n - P.c),
      v_e = - (P.e - P.c),
      v_s = - (P.c - P.s),
      v_w = - (P.c - P.w);

    if (domain_mask(i, j) > 0.5 and v_e <= 0.0 and v_w >= 0.0 and v_n <= 0.0 and v_s >= 0.0) {
      result(i, j) = 1.0;
    } else {
      result(i, j) = 0.0;
    }
  }
}

static void effective_water_velocity(const Geometry &geometry, const array::Vector &water_flux,
                                     array::Vector &result) {

  auto grid = result.grid();

  const auto &cell_type           = geometry.cell_type;
  const auto &bed_elevation       = geometry.bed_elevation;
  const auto &ice_thickness       = geometry.ice_thickness;
  const auto &sea_level_elevation = geometry.sea_level_elevation;

  array::AccessScope list{ &ice_thickness,       &bed_elevation, &cell_type,
                           &sea_level_elevation, &water_flux,    &result };

  double grid_spacing = 0.5 * (grid->dx() + grid->dy()),
         eps          = 1.0; // q_sg regularization

  for (auto p = grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (cell_type.icy(i, j)) {
      // Convert subglacial water flux (m^2/s) to an "effective subglacial freshwater
      // velocity" or flux per unit area of ice front in m/day (see Xu et al 2013, section
      // 2, paragraph 11).
      //
      // [flux] = m^2 / s, so
      // [flux * grid_spacing] = m^3 / s, so
      // [flux * grid_spacing / submerged_front_area] = m / s, and
      // [flux * grid_spacing  * (s / day) / submerged_front_area] = m / day

      double water_depth          = std::max(sea_level_elevation(i, j) - bed_elevation(i, j), 0.0),
             submerged_front_area = water_depth * grid_spacing;

      if (submerged_front_area > 0.0) {
        auto Q_sg = water_flux(i, j) * grid_spacing;
        auto q_sg = Q_sg / std::max(submerged_front_area, eps);

        result(i, j) = q_sg;
      } else {
        result(i, j) = 0.0;
      }
    } else {
      result(i, j) = 0.0;
    }
  } // end of the loop over grid points
}

} // end of namespace diagnostics

EmptyingProblem::EmptyingProblem(std::shared_ptr<const Grid> grid)
  : Component(grid),
    m_potential(grid, "hydraulic_potential"),
    m_tmp(grid, "temporary_storage"),
    m_bottom_surface(grid, "ice_bottom_surface"),
    m_W(grid, "remaining_water_thickness"),
    m_Vstag(grid, "V_staggered"),
    m_Qsum(grid, "flux_total"),
    m_domain_mask(grid, "domain_mask"),
    m_Q(grid, "_water_flux"),
    m_q_sg(grid, "_effective_water_velocity"),
    m_adjustment(grid, "hydraulic_potential_adjustment"),
    m_sinks(grid, "sinks") {

  m_domain_mask.set_interpolation_type(NEAREST);
  m_sinks.set_interpolation_type(NEAREST);

  m_potential.metadata(0)
      .long_name("estimate of the steady state hydraulic potential in the steady hydrology model")
      .units("Pa");

  m_bottom_surface.metadata(0).long_name("ice bottom surface elevation").units("m");

  m_W.metadata(0)
      .long_name(
          "scaled water thickness in the steady state hydrology model (has no physical meaning)")
      .units("m");

  m_Vstag.metadata(0)
      .long_name("water velocity on the staggered grid")
      .units("m s-1");

  m_domain_mask.metadata(0).long_name("mask defining the domain");

  m_Q.metadata(0).long_name("steady state water flux").units("m2 s-1");

  m_q_sg.metadata(0)
      .long_name("x-component of the effective water velocity in the steady-state hydrology model")
      .units("m s-1")
      .output_units("m day-1");
  m_q_sg.metadata(1)
      .long_name("y-component of the effective water velocity in the steady-state hydrology model")
      .units("m s-1")
      .output_units("m day-1");

  m_sinks.metadata(0)
      .long_name("map of sinks in the domain (for debugging)");

  m_adjustment.metadata(0)
      .long_name(
          "potential adjustment needed to fill sinks when computing an estimate of the steady-state hydraulic potential")
      .units("Pa");

  m_eps_gradient = 1e-2;
  m_speed = 1.0;

  m_dx  = m_grid->dx();
  m_dy  = m_grid->dy();
  m_tau = m_config->get_number("hydrology.steady.input_rate_scaling");
}

/*!
 * Compute steady state water flux.
 *
 * @param[in] geometry ice geometry
 * @param[in] no_model_mask no model mask
 * @param[in] water_input_rate water input rate in m/s
 */
void EmptyingProblem::update(const Geometry &geometry,
                             const array::Scalar *no_model_mask,
                             const array::Scalar &water_input_rate,
                             bool recompute_potential) {

  const double
    eps = 1e-16,
    cell_area    = m_grid->cell_area(),
    u_max        = m_speed,
    v_max        = m_speed,
    dt           = 0.5 / (u_max / m_dx + v_max / m_dy), // CFL condition
    volume_ratio = m_config->get_number("hydrology.steady.volume_ratio");

  const int n_iterations = m_config->get_number("hydrology.steady.n_iterations");

  if (recompute_potential) {
    ice_bottom_surface(geometry, m_bottom_surface);

    compute_mask(geometry.cell_type, no_model_mask, m_domain_mask);

    // updates ghosts of m_potential
    compute_potential(geometry.ice_thickness,
                      m_bottom_surface,
                      m_domain_mask,
                      m_potential);

    // diagnostics
    {
      compute_raw_potential(geometry.ice_thickness, m_bottom_surface, m_adjustment);

      m_potential.add(-1.0, m_adjustment, m_adjustment);

      diagnostics::compute_sinks(m_domain_mask, m_potential, m_sinks);
    }
  }

  // set initial state and compute initial volume
  double volume_0 = 0.0;
  {
    array::AccessScope list{&geometry.cell_type, &m_W, &water_input_rate};

    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (geometry.cell_type.icy(i, j)) {
        m_W(i, j) = m_tau * water_input_rate(i, j);
      } else {
        m_W(i, j) = 0.0;
      }

      volume_0 += m_W(i, j);
    }
    volume_0 = cell_area * GlobalSum(m_grid->com, volume_0);
  }
  m_W.update_ghosts();

  // uses ghosts of m_potential and m_domain_mask, updates ghosts of m_Vstag
  compute_velocity(m_potential, m_domain_mask, m_Vstag);

  m_Qsum.set(0.0);

  // no input means no flux
  if (volume_0 == 0.0) {
    m_Q.set(0.0);
    m_q_sg.set(0.0);
    return;
  }

  double volume = 0.0;
  int step_counter = 0;

  array::AccessScope list{&m_Qsum, &m_W, &m_Vstag, &m_domain_mask, &m_tmp};

  for (step_counter = 0; step_counter < n_iterations; ++step_counter) {
    volume = 0.0;

    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      auto v = m_Vstag.star(i, j);
      auto w = m_W.star(i, j);

      double
        q_n = v.n * (v.n >= 0.0 ? w.c : w.n),
        q_e = v.e * (v.e >= 0.0 ? w.c : w.e),
        q_s = v.s * (v.s >= 0.0 ? w.s  : w.c),
        q_w = v.w * (v.w >= 0.0 ? w.w  : w.c),
        divQ = (q_e - q_w) / m_dx + (q_n - q_s) / m_dy;

      // update water thickness
      if (m_domain_mask(i, j) > 0.5) {
        m_tmp(i, j) = w.c + dt * (- divQ);
      } else {
        m_tmp(i, j) = 0.0;
      }

      if (m_tmp(i, j) < -eps) {
        throw RuntimeError::formatted(PISM_ERROR_LOCATION, "W(%d, %d) = %f < 0",
                                      i, j, m_tmp(i, j));
      }

      // accumulate the water flux
      m_Qsum(i, j, 0) += dt * q_e;
      m_Qsum(i, j, 1) += dt * q_n;

      // compute volume
      volume += m_tmp(i, j);
    }

    m_W.copy_from(m_tmp);
    volume = cell_area * GlobalSum(m_grid->com, volume);

    if (volume / volume_0 <= volume_ratio) {
      break;
    }
    m_log->message(3, "%04d V = %f\n", step_counter, volume / volume_0);
  } // end of the loop

  m_log->message(3, "Emptying problem: stopped after %d iterations. V = %f\n",
                 step_counter, volume / volume_0);

  double epsilon = volume / volume_0;

  m_Qsum.update_ghosts();
  staggered_to_regular(geometry.cell_type, m_Qsum,
                       true,    // include floating ice
                       m_Q);
  m_Q.scale(1.0 / (m_tau * (1.0 - epsilon)));

  diagnostics::effective_water_velocity(geometry, m_Q, m_q_sg);
}

/*! Compute the unmodified hydraulic potential (with sinks).
 *
 * @param[in] H ice thickness
 * @param[in] b ice bottom surface elevation
 * @param[out] result simplified hydraulic potential used by to compute velocity
 */
void EmptyingProblem::compute_raw_potential(const array::Scalar &H,
                                            const array::Scalar &b,
                                            array::Scalar &result) const {
  const double
    g     = m_config->get_number("constants.standard_gravity"),
    rho_i = m_config->get_number("constants.ice.density"),
    rho_w = m_config->get_number("constants.fresh_water.density");

  array::AccessScope list({&H, &b, &result});

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    result(i, j) = rho_i * g * H(i, j) + rho_w * g * b(i, j);
  }

  result.update_ghosts();
}

/*!
 * FIXME: uses "result" as temporary storage with ghosts.
 */
void EmptyingProblem::compute_potential(const array::Scalar &ice_thickness,
                                        const array::Scalar &ice_bottom_surface,
                                        const array::Scalar &domain_mask,
                                        array::Scalar1 &result) {
  array::Scalar &psi_new = m_tmp;

  double delta = m_config->get_number("hydrology.steady.potential_delta");

  int n_iterations = m_config->get_number("hydrology.steady.potential_n_iterations");
  int step_counter = 0;
  int n_sinks = 0;
  int n_sinks_remaining = 0;

  compute_raw_potential(ice_thickness, ice_bottom_surface, result);

  array::AccessScope list{&result, &psi_new, &domain_mask};
  for (step_counter = 0; step_counter < n_iterations; ++step_counter) {

    n_sinks_remaining = 0;
    for (auto p = m_grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      if (domain_mask(i, j) > 0.5) {
        auto P = result.star(i, j);

        double
          v_n = - (P.n - P.c),
          v_e = - (P.e - P.c),
          v_s = - (P.c - P.s),
          v_w = - (P.c - P.w);

        if (v_e <= 0.0 and v_w >= 0.0 and v_n <= 0.0 and v_s >= 0.0) {
          ++n_sinks_remaining;

          psi_new(i, j) = 0.25 * (P.n + P.e + P.s + P.w) + delta;
        } else {
          psi_new(i, j) = result(i, j);
        }
      } else {
        psi_new(i, j) = result(i, j);
      }
    }
    // this call updates ghosts of result
    result.copy_from(psi_new);

    n_sinks_remaining = GlobalSum(m_grid->com, n_sinks_remaining);

    // remember the original number of sinks
    if (step_counter == 0) {
      n_sinks = n_sinks_remaining;
    }

    if (n_sinks_remaining == 0) {
      m_log->message(3, "Emptying problem: filled %d sinks after %d iterations.\n",
                     n_sinks - n_sinks_remaining, step_counter);
      break;
    }
  } // end of loop over step_counter

  if (n_sinks_remaining > 0) {
    m_log->message(2, "WARNING: %d sinks left.\n", n_sinks_remaining);
  }
}


static double K(double psi_x, double psi_y, double speed, double epsilon) {
  return speed / std::max(Vector2d(psi_x, psi_y).magnitude(), epsilon);
}

/*!
 * Compute water velocity on the staggered grid.
 */
void EmptyingProblem::compute_velocity(const array::Scalar &psi,
                                       const array::Scalar1 &domain_mask,
                                       array::Staggered &result) const {

  array::AccessScope list{&psi, &result, &domain_mask};

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    for (int o = 0; o < 2; ++o) {
      double psi_x, psi_y;
      if (o == 0) {
        psi_x = (psi(i + 1, j) - psi(i, j)) / m_dx;
        psi_y = (psi(i + 1, j + 1) + psi(i, j + 1) - psi(i + 1, j - 1) - psi(i, j - 1)) / (4.0 * m_dy);
        result(i, j, o) = - K(psi_x, psi_y, m_speed, m_eps_gradient) * psi_x;
      } else {
        psi_x = (psi(i + 1, j + 1) + psi(i + 1, j) - psi(i - 1, j + 1) - psi(i - 1, j)) / (4.0 * m_dx);
        psi_y = (psi(i, j + 1) - psi(i, j)) / m_dy;
        result(i, j, o) = - K(psi_x, psi_y, m_speed, m_eps_gradient) * psi_y;
      }

      auto M = domain_mask.star(i, j);

      if (M.c == 0 and M.e == 0) {
        result(i, j, 0) = 0.0;
      }

      if (M.c == 0 and M.n == 0) {
        result(i, j, 1) = 0.0;
      }
    }
  }
  result.update_ghosts();
}

/*!
 * Compute the mask that defines the domain: ones in the domain, zeroes elsewhere.
 */
void EmptyingProblem::compute_mask(const array::CellType &cell_type,
                                   const array::Scalar *no_model_mask,
                                   array::Scalar &result) const {

  array::AccessScope list{&cell_type, &result};

  if (no_model_mask != nullptr) {
    list.add(*no_model_mask);
  }

  for (auto p = m_grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (not cell_type.ice_free_water(i, j)) {
      result(i, j) = 1.0;
    } else {
      result(i, j) = 0.0;
    }

    if ((no_model_mask != nullptr) and no_model_mask->as_int(i, j) == 1) {
      result(i, j) = 0.0;
    }
  }

  result.update_ghosts();
}



DiagnosticList EmptyingProblem::diagnostics() const {
  return {{"steady_state_hydraulic_potential", Diagnostic::wrap(m_potential)},
          {"hydraulic_potential_adjustment", Diagnostic::wrap(m_adjustment)},
          {"hydraulic_sinks", Diagnostic::wrap(m_sinks)},
          {"remaining_water_thickness", Diagnostic::wrap(m_W)},
          {"effective_water_velocity", Diagnostic::wrap(m_q_sg)}};
}


/*!
 * Remaining water thickness.
 *
 * This field can be used to get an idea of where water is accumulated. (This affects the
 * quality of the estimate of the water flux).
 */
const array::Scalar& EmptyingProblem::remaining_water_thickness() const {
  return m_W;
}

/*!
 * Steady state water flux.
 */
const array::Vector& EmptyingProblem::flux() const {
  return m_Q;
}

/*!
 * Effective water velocity (flux per unit area of the front).
 */
const array::Vector& EmptyingProblem::effective_water_velocity() const {
  return m_q_sg;
}

/*!
 * Hydraulic potential used to determine flow direction.
 */
const array::Scalar& EmptyingProblem::potential() const {
  return m_potential;
}

/*!
 * Map of sinks.
 */
const array::Scalar& EmptyingProblem::sinks() const {
  return m_sinks;
}

/*!
 * Adjustment applied to the unmodified hydraulic potential to eliminate sinks.
 */
const array::Scalar& EmptyingProblem::adjustment() const {
  return m_adjustment;
}

} // end of namespace hydrology
} // end of namespace pism
