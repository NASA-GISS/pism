/* Copyright (C) 2022, 2023 PISM Authors
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

#include "pism/util/array/Staggered.hh"

#include "pism/util/cell_type.hh"

#include "pism/util/pism_utilities.hh"
#include "pism/util/array/CellType.hh"
#include "pism/util/array/Vector.hh"

namespace pism {
namespace array {

Staggered::Staggered(std::shared_ptr<const Grid> grid, const std::string &name)
  : Array(grid, name, WITHOUT_GHOSTS, 2, 1, {0.0}) {
  set_begin_access_use_dof(true);
}

Staggered::Staggered(std::shared_ptr<const Grid> grid, const std::string &name,
                     unsigned int stencil_width)
  : Array(grid, name, WITH_GHOSTS, 2, stencil_width, {0.0}){
  set_begin_access_use_dof(true);
}

void Staggered::copy_from(const Staggered &input) {
  array::AccessScope list {this, &input};
  // FIXME: this should be simplified

  ParallelSection loop(grid()->com);
  try {
    for (auto p = grid()->points(); p; p.next()) {
      const int i = p.i(), j = p.j();

      (*this)(i, j, 0) = input(i, j, 0);
      (*this)(i, j, 1) = input(i, j, 1);
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  update_ghosts();

  inc_state_counter();
}

Staggered1::Staggered1(std::shared_ptr<const Grid> grid, const std::string &name)
    : Staggered(grid, name, 1) {
  // empty
}

std::array<double,2> absmax(const array::Staggered &input) {

  double z[2] = {0.0, 0.0};

  array::AccessScope list(input);
  for (auto p = input.grid()->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    z[0] = std::max(z[0], std::abs(input(i, j, 0)));
    z[1] = std::max(z[1], std::abs(input(i, j, 1)));
  }

  double result[2];
  GlobalMax(input.grid()->com, z, result, 2);

  return {result[0], result[1]};
}

void staggered_to_regular(const array::CellType1 &cell_type,
                          const array::Staggered1 &input,
                          bool include_floating_ice,
                          array::Scalar &result) {

  auto grid = result.grid();

  array::AccessScope list{ &cell_type, &input, &result };

  for (auto p = grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (cell_type.grounded_ice(i, j) or (include_floating_ice and cell_type.icy(i, j))) {
      auto M = cell_type.star_int(i, j);
      auto F = input.star(i, j);

      int n = 0, e = 0, s = 0, w = 0;
      if (include_floating_ice) {
        n = static_cast<int>(cell_type::icy(M.n));
        e = static_cast<int>(cell_type::icy(M.e));
        s = static_cast<int>(cell_type::icy(M.s));
        w = static_cast<int>(cell_type::icy(M.w));
      } else {
        n = static_cast<int>(cell_type::grounded_ice(M.n));
        e = static_cast<int>(cell_type::grounded_ice(M.e));
        s = static_cast<int>(cell_type::grounded_ice(M.s));
        w = static_cast<int>(cell_type::grounded_ice(M.w));
      }

      if (n + e + s + w > 0) {
        result(i, j) = (n * F.n + e * F.e + s * F.s + w * F.w) / (n + e + s + w);
      } else {
        result(i, j) = 0.0;
      }
    } else {
      result(i, j) = 0.0;
    }
  }
}

void staggered_to_regular(const array::CellType1 &cell_type, const array::Staggered1 &input,
                          bool include_floating_ice, array::Vector &result) {

  assert(cell_type.stencil_width() > 0);
  assert(input.stencil_width() > 0);

  auto grid = result.grid();

  array::AccessScope list{ &cell_type, &input, &result };

  for (auto p = grid->points(); p; p.next()) {
    const int i = p.i(), j = p.j();

    auto M = cell_type.star_int(i, j);
    auto F = input.star(i, j);

    int n = 0, e = 0, s = 0, w = 0;
    if (include_floating_ice) {
      n = static_cast<int>(cell_type::icy(M.n));
      e = static_cast<int>(cell_type::icy(M.e));
      s = static_cast<int>(cell_type::icy(M.s));
      w = static_cast<int>(cell_type::icy(M.w));
    } else {
      n = static_cast<int>(cell_type::grounded_ice(M.n));
      e = static_cast<int>(cell_type::grounded_ice(M.e));
      s = static_cast<int>(cell_type::grounded_ice(M.s));
      w = static_cast<int>(cell_type::grounded_ice(M.w));
    }

    if (e + w > 0) {
      result(i, j).u = (e * F.e + w * F.w) / (e + w);
    } else {
      result(i, j).u = 0.0;
    }

    if (n + s > 0) {
      result(i, j).v = (n * F.n + s * F.s) / (n + s);
    } else {
      result(i, j).v = 0.0;
    }
  }
}

} // end of namespace array

} // end of namespace pism
