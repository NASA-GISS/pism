// Copyright (C) 2008--2025 Ed Bueler, Constantine Khroulev, and David Maxwell
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

#include <cassert>

#include <cmath>
#include <cstddef>
#include <memory>
#include <petscdraw.h>
#include <string>
#include <vector>

#include "pism/util/array/Array.hh"
#include "pism/util/array/Array_impl.hh"

#include "pism/util/Config.hh"
#include "pism/util/Grid.hh"
#include "pism/util/Time.hh"

#include "pism/util/Context.hh"
#include "pism/util/Logger.hh"
#include "pism/util/Profiling.hh"
#include "pism/util/VariableMetadata.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/io/File.hh"
#include "pism/util/io/IO_Flags.hh"
#include "pism/util/io/io_helpers.hh"
#include "pism/util/petscwrappers/VecScatter.hh"
#include "pism/util/petscwrappers/Viewer.hh"
#include "pism/util/pism_utilities.hh"

#include "pism/util/InputInterpolation.hh"

namespace pism {

namespace array {

void global_to_local(petsc::DM &dm, Vec source, Vec destination) {
  PetscErrorCode ierr;

  ierr = DMGlobalToLocalBegin(dm, source, INSERT_VALUES, destination);
  PISM_CHK(ierr, "DMGlobalToLocalBegin");

  ierr = DMGlobalToLocalEnd(dm, source, INSERT_VALUES, destination);
  PISM_CHK(ierr, "DMGlobalToLocalEnd");
}

Array::Array(std::shared_ptr<const Grid> grid, const std::string &name, Kind ghostedp, size_t dof,
             size_t stencil_width, const std::vector<double> &zlevels) {
  m_impl  = new Impl();
  m_array = nullptr;

  m_impl->name    = name;
  m_impl->grid    = grid;
  m_impl->ghosted = (ghostedp == WITH_GHOSTS);
  m_impl->dof     = dof;
  m_impl->zlevels = zlevels;

  const auto &config = grid->ctx()->config();

  auto max_stencil_width = static_cast<size_t>(config->get_number("grid.max_stencil_width"));
  if ((dof != 1) or (stencil_width > max_stencil_width)) {
    // use the requested stencil width *if* we have to
    m_impl->da_stencil_width = stencil_width;
  } else {
    // otherwise use the "standard" stencil width
    m_impl->da_stencil_width = max_stencil_width;
  }

  auto system = m_impl->grid->ctx()->unit_system();
  if (m_impl->dof > 1) {
    // dof > 1: this is a 2D vector
    for (unsigned int j = 0; j < m_impl->dof; ++j) {
      m_impl->metadata.push_back({ system, pism::printf("%s[%d]", name.c_str(), j) });
    }
  } else {
    // both 2D and 3D vectors
    m_impl->metadata = { { system, name, zlevels } };
  }

  if (zlevels.size() > 1) {
    m_impl->bsearch_accel = gsl_interp_accel_alloc();
    if (m_impl->bsearch_accel == NULL) {
      throw RuntimeError(PISM_ERROR_LOCATION, "Failed to allocate a GSL interpolation accelerator");
    }
  }
}

Array::~Array() {
  assert(m_impl->access_counter == 0);

  if (m_impl->bsearch_accel != nullptr) {
    gsl_interp_accel_free(m_impl->bsearch_accel);
    m_impl->bsearch_accel = nullptr;
  }

  delete m_impl;
  m_impl = nullptr;
}

//! \brief Get the object state counter.
/*!
 * This method returns the "revision number" of an Array.
 *
 * It can be used to determine it a field was updated and if a certain
 * computation needs to be re-done. One example is computing the smoothed bed
 * for the SIA computation, which is only necessary if the bed deformation code
 * fired.
 *
 * See also inc_state_counter().
 */
int Array::state_counter() const {
  return m_impl->state_counter;
}

std::shared_ptr<const Grid> Array::grid() const {
  return m_impl->grid;
}

unsigned int Array::ndof() const {
  return m_impl->dof;
}

const std::vector<double> &Array::levels() const {
  return m_impl->zlevels;
}

//! \brief Increment the object state counter.
/*!
 * See the documentation of get_state_counter(). This method is the
 * *only* way to manually increment the state counter. It is also
 * automatically updated by Array methods that are known to
 * change stored values.
 */
void Array::inc_state_counter() {
  m_impl->state_counter++;
}

//! Returns the number of spatial dimensions.
unsigned int Array::ndims() const {
  return m_impl->zlevels.size() > 1 ? 3 : 2;
}

std::vector<int> Array::shape() const {

  auto grid = m_impl->grid;

  if (ndims() == 3) {
    return { (int)grid->My(), (int)grid->Mx(), (int)levels().size() };
  }

  if (ndof() == 1) {
    return { (int)grid->My(), (int)grid->Mx() };
  }

  return { (int)grid->My(), (int)grid->Mx(), (int)ndof() };
}

void Array::set_begin_access_use_dof(bool flag) {
  m_impl->begin_access_use_dof = flag;
}

void Array::set_interpolation_type(InterpolationType type) {
  if (not(type == LINEAR or type == NEAREST)) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "invalid interpolation type: %d", (int)type);
  }
  m_impl->interpolation_type = type;
}

/** Convert from `int` to PETSc's `NormType`.
 *
 *
 * @param[in] input norm type as an integer
 *
 * @return norm type as PETSc's `NormType`.
 */
static NormType int_to_normtype(int input) {
  switch (input) {
  case NORM_1:
    return NORM_1;
  case NORM_2:
    return NORM_2;
  case NORM_INFINITY:
    return NORM_INFINITY;
  default:
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "invalid norm type: %d", input);
  }
}

//! Result: v <- v + alpha * x. Calls VecAXPY.
void Array::add(double alpha, const Array &x) {
  checkCompatibility("add", x);

  PetscErrorCode ierr = VecAXPY(vec(), alpha, x.vec());
  PISM_CHK(ierr, "VecAXPY");

  inc_state_counter(); // mark as modified
}

//! Result: v[j] <- v[j] + alpha for all j. Calls VecShift.
void Array::shift(double alpha) {
  PetscErrorCode ierr = VecShift(vec(), alpha);
  PISM_CHK(ierr, "VecShift");

  inc_state_counter(); // mark as modified
}

//! Result: v <- v * alpha. Calls VecScale.
void Array::scale(double alpha) {
  PetscErrorCode ierr = VecScale(vec(), alpha);
  PISM_CHK(ierr, "VecScale");

  inc_state_counter(); // mark as modified
}

//! Copies v to a global vector 'destination'. Ghost points are discarded.
/*! This is potentially dangerous: make sure that `destination` has the same
    dimensions as the current Array.

    DMLocalToGlobalBegin/End is broken in PETSc 3.5, so we roll our
    own.
 */
void Array::copy_to_vec(std::shared_ptr<petsc::DM> destination_da, petsc::Vec &destination) const {
  // m_dof > 1 for vector, staggered grid 2D fields, etc. In this case
  // zlevels.size() == 1. For 3D fields, m_dof == 1 (all 3D fields are
  // scalar) and zlevels.size() corresponds to dof of the underlying PETSc
  // DM object. So we want the bigger of the two numbers here.
  unsigned int N = std::max((size_t)m_impl->dof, m_impl->zlevels.size());

  this->get_dof(destination_da, destination, 0, N);
}

void Array::get_dof(std::shared_ptr<petsc::DM> da_result, petsc::Vec &result, unsigned int start,
                    unsigned int count) const {
  if (start >= m_impl->dof) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "invalid argument (start); got %d", start);
  }

  petsc::DMDAVecArrayDOF tmp_res(da_result, result), tmp_v(dm(), vec());

  double ***result_a = static_cast<double ***>(tmp_res.get()),
         ***source_a = static_cast<double ***>(tmp_v.get());

  ParallelSection loop(m_impl->grid->com);
  try {
    for (auto p = m_impl->grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();
      PetscErrorCode ierr =
          PetscMemcpy(result_a[j][i], &source_a[j][i][start], count * sizeof(PetscScalar));
      PISM_CHK(ierr, "PetscMemcpy");
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();
}

void Array::set_dof(std::shared_ptr<petsc::DM> da_source, petsc::Vec &source, unsigned int start,
                    unsigned int count) {
  if (start >= m_impl->dof) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "invalid argument (start); got %d", start);
  }

  petsc::DMDAVecArrayDOF tmp_src(da_source, source), tmp_v(dm(), vec());

  double ***source_a = static_cast<double ***>(tmp_src.get()),
         ***result_a = static_cast<double ***>(tmp_v.get());

  ParallelSection loop(m_impl->grid->com);
  try {
    for (auto p = m_impl->grid->points(); p; p.next()) {
      const int i = p.i(), j = p.j();
      PetscErrorCode ierr =
          PetscMemcpy(&result_a[j][i][start], source_a[j][i], count * sizeof(PetscScalar));
      PISM_CHK(ierr, "PetscMemcpy");
    }
  } catch (...) {
    loop.failed();
  }
  loop.check();

  inc_state_counter(); // mark as modified
}

//! @brief Get the stencil width of the current Array. Returns 0
//! if ghosts are not available.
unsigned int Array::stencil_width() const {
  if (m_impl->ghosted) {
    return m_impl->da_stencil_width;
  }

  return 0;
}

petsc::Vec &Array::vec() const {
  if (m_impl->v.get() == nullptr) {
    PetscErrorCode ierr = 0;
    if (m_impl->ghosted) {
      ierr = DMCreateLocalVector(*dm(), m_impl->v.rawptr());
      PISM_CHK(ierr, "DMCreateLocalVector");
    } else {
      ierr = DMCreateGlobalVector(*dm(), m_impl->v.rawptr());
      PISM_CHK(ierr, "DMCreateGlobalVector");
    }
  }
  return m_impl->v;
}

std::shared_ptr<petsc::DM> Array::dm() const {
  if (m_impl->da == nullptr) {
    // dof > 1 for vector, staggered grid 2D fields, etc. In this case zlevels.size() ==
    // 1. For 3D fields, dof == 1 (all 3D fields are scalar) and zlevels.size()
    // corresponds to dof of the underlying PETSc DM object.
    auto da_dof = std::max((unsigned int)m_impl->zlevels.size(), m_impl->dof);

    // initialize the da member:
    m_impl->da = grid()->get_dm(da_dof, m_impl->da_stencil_width);
  }
  return m_impl->da;
}

//! Sets the variable name to `name`.
/**
 * This is the "overall" name of a field. This is **not** the same as the
 * NetCDF variable name. Use `metadata(...).set_name(...)` to set that.
 */
void Array::set_name(const std::string &name) {
  m_impl->name = name;
}

//! @brief Get the name of an Array object.
/**
 * This is the name used to refer to this object in PISM (e.g. via the
 * Vars class), **not** the name of the corresponding NetCDF variable.
 * (The problem is that one Array instance may correspond to
 * several NetCDF variables. Use metadata(...).get_name() to get the
 * name of NetCDF variables an Array is saved to.)
 */
const std::string &Array::get_name() const {
  return m_impl->name;
}

void set_default_value_or_stop(const VariableMetadata &variable, io::Default default_value,
                               const Logger &log, Vec output) {

  if (default_value.exists()) {
    // If it is optional, fill with the provided default value.
    log.message(2, "  variable %s (%s) is not found: using the default constant\n",
                variable.get_name().c_str(), variable.get_string("long_name").c_str());

    PetscErrorCode ierr = VecSet(output, default_value);
    PISM_CHK(ierr, "VecSet");
  } else {
    // if it's critical, print an error message and stop
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Can't find variable '%s'",
                                  variable.get_name().c_str());
  }
}

//! Gets an Array from a file `file`, interpolating onto the current grid.
/*! Stops if the variable was not found and `critical` == true.
 */
void Array::regrid_impl(const File &file, io::Default default_value) {

  assert(ndims() == 2);

  auto log = grid()->ctx()->log();

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  auto dm_for_io = ndof() == 1 ? dm() : grid()->get_dm(1, 0);

  // a temporary one-component vector that uses a compatible domain decomposition
  petsc::TemporaryGlobalVec tmp(dm_for_io);

  for (unsigned int j = 0; j < ndof(); ++j) {
    auto variable = metadata(j);

    auto V = file.find_variable(variable.get_name(), variable["standard_name"]);

    if (V.exists) {
      auto interp = grid()->get_interpolation(levels(), file, V.name, m_impl->interpolation_type);

      int last_record = -1;
      interp->regrid(variable, file, last_record, *grid(), tmp);
    } else {
      set_default_value_or_stop(variable, default_value, *log, tmp);
    }

    set_dof(dm_for_io, tmp, j);
  } // end of the loop over degrees of freedom

  // The calls above only set the values owned by a processor, so we need to
  // communicate if m_has_ghosts == true:
  if (m_impl->ghosted) {
    update_ghosts();
  }
}

//! Reads appropriate NetCDF variable(s) into an Array.
void Array::read_impl(const File &file, const unsigned int time) {

  auto log = grid()->ctx()->log();
  log->message(4, "  Reading %s...\n", m_impl->name.c_str());

  if (ndof() == 1) {
    // This takes are of scalar variables (both 2D and 3D).
    if (m_impl->ghosted) {
      petsc::TemporaryGlobalVec tmp(dm());

      petsc::VecArray tmp_array(tmp);
      io::read_spatial_variable(metadata(0), *grid(), file, time, tmp_array.get());

      global_to_local(*dm(), tmp, vec());
    } else {
      petsc::VecArray v_array(vec());
      io::read_spatial_variable(metadata(0), *grid(), file, time, v_array.get());
    }
    return;
  }

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  auto da2 = grid()->get_dm(1, 0);

  // A temporary one-component vector, distributed across processors
  // the same way v is
  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int j = 0; j < ndof(); ++j) {

    {
      petsc::VecArray tmp_array(tmp);
      io::read_spatial_variable(metadata(j), *grid(), file, time, tmp_array.get());
    }

    set_dof(da2, tmp, j);
  }

  // The calls above only set the values owned by a processor, so we need to
  // communicate if m_impl->ghosted is true:
  if (m_impl->ghosted) {
    update_ghosts();
  }
}

//! \brief Define variables corresponding to an Array in a file opened using `file`.
void Array::define(const File &file, io::Type default_type) const {
  for (unsigned int j = 0; j < ndof(); ++j) {
    io::Type type = metadata(j).get_output_type();
    if (type == io::PISM_NAT) {
      type = default_type;
    }

    io::define_spatial_variable(metadata(j), *m_impl->grid, file, type);
  }
}

//! @brief Returns a reference to the SpatialVariableMetadata object
//! containing metadata for the compoment N.
SpatialVariableMetadata& Array::metadata(unsigned int N) {
  assert(N < m_impl->dof);
  return m_impl->metadata[N];
}

const SpatialVariableMetadata& Array::metadata(unsigned int N) const {
  assert(N < m_impl->dof);
  return m_impl->metadata[N];
}

//! Writes an Array to a NetCDF file.
void Array::write_impl(const File &file) const {
  auto log = m_impl->grid->ctx()->log();
  auto time = timestamp(m_impl->grid->com);

  // The simplest case:
  if (ndof() == 1) {
    log->message(3, "[%s] Writing %s...\n",
                 time.c_str(), metadata(0).get_name().c_str());

    if (m_impl->ghosted) {
      petsc::TemporaryGlobalVec tmp(dm());

      this->copy_to_vec(dm(), tmp);

      petsc::VecArray tmp_array(tmp);

      io::write_spatial_variable(metadata(0), *grid(), file, tmp_array.get());
    } else {
      petsc::VecArray v_array(vec());
      io::write_spatial_variable(metadata(0), *grid(), file, v_array.get());
    }
    return;
  }

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  auto da2 = m_impl->grid->get_dm(1, 0);

  // a temporary one-component vector, distributed across processors
  // the same way v is
  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int j = 0; j < ndof(); ++j) {
    get_dof(da2, tmp, j);

    petsc::VecArray tmp_array(tmp);
    log->message(3, "[%s] Writing %s...\n",
                 time.c_str(), metadata(j).get_name().c_str());
    io::write_spatial_variable(metadata(j), *grid(), file, tmp_array.get());
  }
}

//! Dumps a variable to a file, overwriting this file's contents (for debugging).
void Array::dump(const char filename[]) const {
  File file(m_impl->grid->com, filename,
            string_to_backend(m_impl->grid->ctx()->config()->get_string("output.format")),
            io::PISM_READWRITE_CLOBBER);

  if (not m_impl->metadata[0].get_time_independent()) {
    io::define_time(file, *m_impl->grid->ctx());
    io::append_time(file, *m_impl->grid->ctx()->config(), m_impl->grid->ctx()->time()->current());
  }

  define(file, io::PISM_DOUBLE);
  write(file);
}

//! Checks if two Arrays have compatible sizes, dimensions and numbers of degrees of freedom.
void Array::checkCompatibility(const char* func, const Array &other) const {
  PetscErrorCode ierr;
  // We have to use PetscInt because of VecGetSizes below.
  PetscInt X_size, Y_size;

  if (m_impl->dof != other.m_impl->dof) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Array::%s(...): operands have different numbers of degrees of freedom",
                                  func);
  }

  ierr = VecGetSize(vec(), &X_size);
  PISM_CHK(ierr, "VecGetSize");

  ierr = VecGetSize(other.vec(), &Y_size);
  PISM_CHK(ierr, "VecGetSize");

  if (X_size != Y_size) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "Array::%s(...): incompatible Vec sizes (called as %s.%s(%s))",
                                  func, m_impl->name.c_str(), func, other.m_impl->name.c_str());
  }
}

//! Checks if an Array is allocated and calls DAVecGetArray.
void  Array::begin_access() const {

  if (m_impl->access_counter < 0) {
    throw RuntimeError(PISM_ERROR_LOCATION, "Array::begin_access(): m_access_counter < 0");
  }

  if (m_impl->access_counter == 0) {
    PetscErrorCode ierr;
    if (m_impl->begin_access_use_dof) {
      ierr = DMDAVecGetArrayDOF(*dm(), vec(), &m_array);
      PISM_CHK(ierr, "DMDAVecGetArrayDOF");
    } else {
      ierr = DMDAVecGetArray(*dm(), vec(), &m_array);
      PISM_CHK(ierr, "DMDAVecGetArray");
    }
  }

  m_impl->access_counter++;
}

//! Checks if an Array is allocated and calls DAVecRestoreArray.
void  Array::end_access() const {
  PetscErrorCode ierr;

  if (m_array == NULL) {
    throw RuntimeError(PISM_ERROR_LOCATION,
                       "Array::end_access(): a == NULL (looks like begin_access() was not called)");
  }

  if (m_impl->access_counter < 0) {
    throw RuntimeError(PISM_ERROR_LOCATION, "Array::end_access(): m_access_counter < 0");
  }

  m_impl->access_counter--;
  if (m_impl->access_counter == 0) {
    if (m_impl->begin_access_use_dof) {
      ierr = DMDAVecRestoreArrayDOF(*dm(), vec(), &m_array);
      PISM_CHK(ierr, "DMDAVecRestoreArrayDOF");
    } else {
      ierr = DMDAVecRestoreArray(*dm(), vec(), &m_array);
      PISM_CHK(ierr, "DMDAVecRestoreArray");
    }
    m_array = NULL;
  }
}

//! Updates ghost points.
void  Array::update_ghosts() {
  PetscErrorCode ierr;
  if (not m_impl->ghosted) {
    return;
  }

  ierr = DMLocalToLocalBegin(*dm(), vec(), INSERT_VALUES, vec());
  PISM_CHK(ierr, "DMLocalToLocalBegin");

  ierr = DMLocalToLocalEnd(*dm(), vec(), INSERT_VALUES, vec());
  PISM_CHK(ierr, "DMLocalToLocalEnd");
}

//! Result: v[j] <- c for all j.
void  Array::set(const double c) {
  PetscErrorCode ierr = VecSet(vec(),c);
  PISM_CHK(ierr, "VecSet");

  inc_state_counter();          // mark as modified
}

void Array::check_array_indices(int i, int j, unsigned int k) const {
  double ghost_width = 0;
  if (m_impl->ghosted) {
    ghost_width = m_impl->da_stencil_width;
  }
  // m_impl->dof > 1 for vector, staggered grid 2D fields, etc. In this case
  // zlevels.size() == 1. For 3D fields, m_impl->dof == 1 (all 3D fields are
  // scalar) and zlevels.size() corresponds to dof of the underlying PETSc
  // DM object. So we want the bigger of the two numbers here.
  unsigned int N = std::max((size_t)m_impl->dof, m_impl->zlevels.size());

  bool out_of_range = (i < m_impl->grid->xs() - ghost_width) ||
    (i > m_impl->grid->xs() + m_impl->grid->xm() + ghost_width) ||
    (j < m_impl->grid->ys() - ghost_width) ||
    (j > m_impl->grid->ys() + m_impl->grid->ym() + ghost_width) ||
    (k >= N);

  if (out_of_range) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "%s(%d, %d, %d) is out of bounds",
                                  m_impl->name.c_str(), i, j, k);
  }

  if (m_array == NULL) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "%s: begin_access() was not called", m_impl->name.c_str());
  }
}

//! Computes the norm of all the components of an Array.
/*!
See comment for range(); because local Vecs are VECSEQ, needs a reduce operation.
See src/trypetsc/localVecMax.c.
 */
std::vector<double> Array::norm(int n) const {
  std::vector<double> result(m_impl->dof);

  NormType type = int_to_normtype(n);

  if (m_impl->dof > 1) {
    PetscErrorCode ierr = VecStrideNormAll(vec(), type, result.data());
    PISM_CHK(ierr, "VecStrideNormAll");
  } else {
    PetscErrorCode ierr = VecNorm(vec(), type, result.data());
    PISM_CHK(ierr, "VecNorm");
  }

  if (m_impl->ghosted) {
    // needs a reduce operation; use GlobalMax() if NORM_INFINITY,
    // otherwise GlobalSum; carefully in NORM_2 case
    switch (type) {
    case NORM_1_AND_2: {
      throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                    "Array::norm_all(...): NORM_1_AND_2"
                                    " not implemented (called as %s.norm_all(...))",
                                    m_impl->name.c_str());
    }
    case NORM_1: {
      for (unsigned int k = 0; k < m_impl->dof; ++k) {
        result[k] = GlobalSum(m_impl->grid->com, result[k]);
      }
      return result;
    }
    case NORM_2: {
      for (unsigned int k = 0; k < m_impl->dof; ++k) {
        // undo sqrt in VecNorm before sum; sum up; take sqrt
        result[k] = sqrt(GlobalSum(m_impl->grid->com, result[k] * result[k]));
      }
      return result;
    }
    case NORM_INFINITY: {
      for (unsigned int k = 0; k < m_impl->dof; ++k) {
        result[k] = GlobalMax(m_impl->grid->com, result[k]);
      }
      return result;
    }
    default: {
      throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                    "Array::norm_all(...): unknown norm type"
                                    " (called as %s.norm_all(...))",
                                    m_impl->name.c_str());
    }
    } // switch
  } else {
    return result;
  }
}

void Array::write(const std::string &filename) const {
  // We expect the file to be present and ready to write into.
  File file(m_impl->grid->com, filename,
            string_to_backend(m_impl->grid->ctx()->config()->get_string("output.format")),
            io::PISM_READWRITE);

  this->write(file);
}

void Array::read(const std::string &filename, unsigned int time) {
  File file(m_impl->grid->com, filename, io::PISM_GUESS, io::PISM_READONLY);
  this->read(file, time);
}

void Array::regrid(const std::string &filename, io::Default default_value) {
  File file(m_impl->grid->com, filename, io::PISM_GUESS, io::PISM_READONLY);
  this->regrid(file, default_value);
}

static void check_range(petsc::Vec &v,
                        const SpatialVariableMetadata &metadata,
                        const std::string &filename,
                        const Logger &log,
                        bool report_range) {
  PetscErrorCode ierr = 0;

  double min = 0.0;
  double max = 0.0;

  ierr = VecMin(v, NULL, &min);
  PISM_CHK(ierr, "VecMin");
  ierr = VecMax(v, NULL, &max);
  PISM_CHK(ierr, "VecMax");

  // Check the range and warn the user if needed:
  metadata.check_range(filename, min, max);
  if (report_range) {
    metadata.report_range(log, min, max);
  }
}


/** Read a field from a file, interpolating onto the current grid.
 *
 * When `flag` is set to `CRITICAL`, stop if could not find the variable
 * in the provided input file; `default_value` is ignored.
 *
 * When `flag` is set to `OPTIONAL`, fill this Array with
 * `default_value` if could not find the variable in the provided
 * input file.
 *
 * When `flag` is set to `CRITICAL_FILL_MISSING`, replace missing
 * values matching the `_FillValue` attribute with `default_value`,
 * stop if could not find the variable.
 *
 * When `flag` is set to `OPTIONAL_FILL_MISSING`, replace missing
 * values matching the `_FillValue` attribute with `default_value`;
 * fill the whole Array with `default_value` if could not find
 * the variable.
 *
 * @param file input file
 * @param flag regridding mode, see above
 * @param default_value default value, meaning depends on the
 *        regridding mode flag
 *
 * @return 0 on success
 */
void Array::regrid(const File &file, io::Default default_value) {
  auto log = m_impl->grid->ctx()->log();

  log->message(3, "  [%s] Regridding %s...\n", timestamp(m_impl->grid->com).c_str(),
               m_impl->name.c_str());
  double start_time = get_time(m_impl->grid->com);
  m_impl->grid->ctx()->profiling().begin("io.regridding");
  try {
    this->regrid_impl(file, default_value);
    inc_state_counter();          // mark as modified

    if (ndims() == 2) {
      for (unsigned k = 0; k < ndof(); ++k) {
        auto dm = grid()->get_dm(1, 0);

        petsc::TemporaryGlobalVec v(dm);

        get_dof(dm, v, k);

        check_range(v, metadata(k), file.name(), *log, m_impl->report_range);
      }
    } else {
      check_range(vec(), metadata(0), file.name(), *log, m_impl->report_range);
    }


  } catch (RuntimeError &e) {
    e.add_context("regridding '%s' from '%s'",
                  this->get_name().c_str(), file.name().c_str());
    throw;
  }
  m_impl->grid->ctx()->profiling().end("io.regridding");
  double
    end_time   = get_time(m_impl->grid->com),
    time_spent = end_time - start_time;

  if (time_spent > 1.0) {
    log->message(3, "  done in %f seconds.\n", time_spent);
  } else {
    log->message(3, "  done.\n");
  }
}

void Array::read(const File &file, const unsigned int time) {
  this->read_impl(file, time);
  inc_state_counter();          // mark as modified
}

void Array::write(const File &file) const {
  define(file, io::PISM_DOUBLE);

  MPI_Comm com = m_impl->grid->com;
  double start_time = get_time(com);
  write_impl(file);
  double end_time = get_time(com);

  const double
    minute     = 60.0,          // one minute in seconds
    time_spent = end_time - start_time,
    megabyte   = pow(2, 20),
    N          = static_cast<double>(size()),
    mb_double  = static_cast<double>(sizeof(double)) * N / megabyte,
    mb_float   = static_cast<double>(sizeof(float)) * N / megabyte;

  std::string timestamp = pism::timestamp(com);
  std::string spacer(timestamp.size(), ' ');
  if (time_spent > 1) {
    m_impl->grid->ctx()->log()->message(3,
                                  "\n"
                                  "  [%s] Done writing %s (%f Mb double, %f Mb float)\n"
                                  "   %s  in %f seconds (%f minutes).\n"
                                  "   %s  Effective throughput: double: %f Mb/s, float: %f Mb/s.\n",
                                  timestamp.c_str(), m_impl->name.c_str(), mb_double, mb_float,
                                  spacer.c_str(), time_spent, time_spent / minute,
                                  spacer.c_str(),
                                  mb_double / time_spent, mb_float / time_spent);
  } else {
    m_impl->grid->ctx()->log()->message(3, " done.\n");
  }
}

AccessScope::AccessScope() {
  // empty
}

AccessScope::~AccessScope() {
  while (not m_vecs.empty()) {
    try {
      m_vecs.back()->end_access();
      m_vecs.pop_back();
    } catch (...) {
      handle_fatal_errors(MPI_COMM_SELF);
    }
  }
}

AccessScope::AccessScope(std::initializer_list<const PetscAccessible *> vecs) {
  for (const auto *j : vecs) {
    assert(j != nullptr);
    add(*j);
  }
}

AccessScope::AccessScope(const PetscAccessible &vec) {
  add(vec);
}

void AccessScope::add(const PetscAccessible &vec) {
  vec.begin_access();
  m_vecs.push_back(&vec);
}

void AccessScope::add(const std::vector<const PetscAccessible*> &vecs) {
  for (const auto *v : vecs) {
    assert(v != nullptr);
    add(*v);
  }
}

//! Return the total number of elements in the *owned* part of an array.
size_t Array::size() const {
  // m_impl->dof > 1 for vector, staggered grid 2D fields, etc. In this case
  // zlevels.size() == 1. For 3D fields, m_impl->dof == 1 (all 3D fields are
  // scalar) and zlevels.size() corresponds to dof of the underlying PETSc
  // DM object.

  size_t
    Mx = m_impl->grid->Mx(),
    My = m_impl->grid->My(),
    Mz = m_impl->zlevels.size(),
    dof = m_impl->dof;

  return Mx * My * Mz * dof;
}

/*! Allocate a copy on processor zero and the scatter needed to move data.
 */
std::shared_ptr<petsc::Vec> Array::allocate_proc0_copy() const {
  PetscErrorCode ierr;
  Vec v_proc0 = NULL;
  Vec result = NULL;

  ierr = PetscObjectQuery((PetscObject)dm()->get(), "v_proc0", (PetscObject*)&v_proc0);
  PISM_CHK(ierr, "PetscObjectQuery")
                                                                                          ;
  if (v_proc0 == NULL) {

    // natural_work will be destroyed at the end of scope, but it will
    // only decrement the reference counter incremented by
    // PetscObjectCompose below.
    petsc::Vec natural_work;
    // create a work vector with natural ordering:
    ierr = DMDACreateNaturalVector(*dm(), natural_work.rawptr());
    PISM_CHK(ierr, "DMDACreateNaturalVector");

    // this increments the reference counter of natural_work
    ierr = PetscObjectCompose((PetscObject)dm()->get(), "natural_work",
                              (PetscObject)((::Vec)natural_work));
    PISM_CHK(ierr, "PetscObjectCompose");

    // scatter_to_zero will be destroyed at the end of scope, but it
    // will only decrement the reference counter incremented by
    // PetscObjectCompose below.
    petsc::VecScatter scatter_to_zero;

    // initialize the scatter to processor 0 and create storage on processor 0
    ierr = VecScatterCreateToZero(natural_work, scatter_to_zero.rawptr(),
                                  &v_proc0);
    PISM_CHK(ierr, "VecScatterCreateToZero");

    // this increments the reference counter of scatter_to_zero
    ierr = PetscObjectCompose((PetscObject)dm()->get(), "scatter_to_zero",
                              (PetscObject)((::VecScatter)scatter_to_zero));
    PISM_CHK(ierr, "PetscObjectCompose");

    // this increments the reference counter of v_proc0
    ierr = PetscObjectCompose((PetscObject)dm()->get(), "v_proc0",
                              (PetscObject)v_proc0);
    PISM_CHK(ierr, "PetscObjectCompose");

    // We DO NOT call VecDestroy(v_proc0): the petsc::Vec wrapper will
    // take care of this.
    result = v_proc0;
  } else {
    // We DO NOT call VecDestroy(result): the petsc::Vec wrapper will
    // take care of this.
    ierr = VecDuplicate(v_proc0, &result);
    PISM_CHK(ierr, "VecDuplicate");
  }
  return std::shared_ptr<petsc::Vec>(new petsc::Vec(result));
}

void Array::put_on_proc0(petsc::Vec &parallel, petsc::Vec &onp0) const {
  PetscErrorCode ierr = 0;
  VecScatter scatter_to_zero = NULL;
  Vec natural_work = NULL;

  ierr = PetscObjectQuery((PetscObject)dm()->get(), "scatter_to_zero",
                          (PetscObject*)&scatter_to_zero);
  PISM_CHK(ierr, "PetscObjectQuery");

  ierr = PetscObjectQuery((PetscObject)dm()->get(), "natural_work",
                          (PetscObject*)&natural_work);
  PISM_CHK(ierr, "PetscObjectQuery");

  if (natural_work == NULL || scatter_to_zero == NULL) {
    throw RuntimeError(PISM_ERROR_LOCATION, "call allocate_proc0_copy() before calling put_on_proc0");
  }

  ierr = DMDAGlobalToNaturalBegin(*dm(), parallel, INSERT_VALUES, natural_work);
  PISM_CHK(ierr, "DMDAGlobalToNaturalBegin");

  ierr = DMDAGlobalToNaturalEnd(*dm(), parallel, INSERT_VALUES, natural_work);
  PISM_CHK(ierr, "DMDAGlobalToNaturalEnd");

  ierr = VecScatterBegin(scatter_to_zero, natural_work, onp0,
                         INSERT_VALUES, SCATTER_FORWARD);
  PISM_CHK(ierr, "VecScatterBegin");

  ierr = VecScatterEnd(scatter_to_zero, natural_work, onp0,
                       INSERT_VALUES, SCATTER_FORWARD);
  PISM_CHK(ierr, "VecScatterEnd");
}


//! Puts a local array::Scalar on processor 0.
void Array::put_on_proc0(petsc::Vec &onp0) const {
  if (m_impl->ghosted) {
    petsc::TemporaryGlobalVec tmp(dm());
    this->copy_to_vec(dm(), tmp);
    put_on_proc0(tmp, onp0);
  } else {
    put_on_proc0(vec(), onp0);
  }
}

void Array::get_from_proc0(petsc::Vec &onp0, petsc::Vec &parallel) const {
  PetscErrorCode ierr;

  VecScatter scatter_to_zero = NULL;
  Vec natural_work = NULL;
  ierr = PetscObjectQuery((PetscObject)dm()->get(), "scatter_to_zero",
                          (PetscObject*)&scatter_to_zero);
  PISM_CHK(ierr, "PetscObjectQuery");

  ierr = PetscObjectQuery((PetscObject)dm()->get(), "natural_work",
                          (PetscObject*)&natural_work);
  PISM_CHK(ierr, "PetscObjectQuery");

  if (natural_work == NULL || scatter_to_zero == NULL) {
    throw RuntimeError(PISM_ERROR_LOCATION, "call allocate_proc0_copy() before calling get_from_proc0");
  }

  ierr = VecScatterBegin(scatter_to_zero, onp0, natural_work,
                         INSERT_VALUES, SCATTER_REVERSE);
  PISM_CHK(ierr, "VecScatterBegin");

  ierr = VecScatterEnd(scatter_to_zero, onp0, natural_work,
                       INSERT_VALUES, SCATTER_REVERSE);
  PISM_CHK(ierr, "VecScatterEnd");

  ierr = DMDANaturalToGlobalBegin(*dm(), natural_work, INSERT_VALUES, parallel);
  PISM_CHK(ierr, "DMDANaturalToGlobalBegin");

  ierr = DMDANaturalToGlobalEnd(*dm(), natural_work, INSERT_VALUES, parallel);
  PISM_CHK(ierr, "DMDANaturalToGlobalEnd");
}

//! Gets a local Array2 from processor 0.
void Array::get_from_proc0(petsc::Vec &onp0) {
  if (m_impl->ghosted) {
    petsc::TemporaryGlobalVec tmp(dm());
    get_from_proc0(onp0, tmp);
    global_to_local(*dm(), tmp, vec());
  } else {
    get_from_proc0(onp0, vec());
  }
  inc_state_counter();          // mark as modified
}

/*!
 * Copy data to rank 0 and compute the checksum.
 *
 * Does not use ghosts. Results should be independent of the parallel domain
 * decomposition.
 */
uint64_t Array::fletcher64_serial() const {

  auto v = allocate_proc0_copy();
  put_on_proc0(*v);

  MPI_Comm com = m_impl->grid->ctx()->com();

  int rank = 0;
  MPI_Comm_rank(com, &rank);

  uint64_t result = 0;
  if (rank == 0) {
    petsc::VecArray array(*v);

    PetscInt size = 0;
    PetscErrorCode ierr = VecGetLocalSize(*v, &size);
    PISM_CHK(ierr, "VecGetLocalSize");

    result = pism::fletcher64((uint32_t*)array.get(), static_cast<size_t>(size) * 2);
  }
  MPI_Bcast(&result, 1, MPI_UINT64_T, 0, com);

  return result;
}

/*!
 * Compute a checksum of a vector.
 *
 * The result depends on the number of processors used.
 *
 * We assume that sizeof(double) == 2 * sizeof(uint32_t), i.e. double uses 64 bits.
 */
uint64_t Array::fletcher64() const {
  static_assert(sizeof(double) == 2 * sizeof(uint32_t),
                "Cannot compile Array::fletcher64() (sizeof(double) != 2 * sizeof(uint32_t))");

  MPI_Status mpi_stat;
  const int checksum_tag = 42;

  MPI_Comm com = m_impl->grid->ctx()->com();

  int rank = 0;
  MPI_Comm_rank(com, &rank);

  int comm_size = 0;
  MPI_Comm_size(com, &comm_size);

  PetscInt local_size = 0;
  PetscErrorCode ierr = VecGetLocalSize(vec(), &local_size); PISM_CHK(ierr, "VecGetLocalSize");
  uint64_t sum = 0;
  {
    petsc::VecArray v(vec());
    // compute checksums for local patches on all ranks
    sum = pism::fletcher64((uint32_t*)v.get(), static_cast<size_t>(local_size) * 2);
  }

  if (rank == 0) {
    std::vector<uint64_t> sums(comm_size);

    // gather checksums of patches on rank 0
    sums[0] = sum;
    for (int r = 1; r < comm_size; ++r) {
      MPI_Recv(&sums[r], 1, MPI_UINT64_T, r, checksum_tag, com, &mpi_stat);
    }

    // compute the checksum of checksums
    sum = pism::fletcher64((uint32_t*)sums.data(), static_cast<size_t>(comm_size) * 2);
  } else {
    MPI_Send(&sum, 1, MPI_UINT64_T, 0, checksum_tag, com);
  }

  // broadcast to all ranks
  MPI_Bcast(&sum, 1, MPI_UINT64_T, 0, com);

  return sum;
}

std::string Array::checksum(bool serial) const {
  if (serial) {
  // unsigned long long is supposed to be at least 64 bit long
    return pism::printf("%016llx", (unsigned long long int)this->fletcher64_serial());
  }
  // unsigned long long is supposed to be at least 64 bit long
  return pism::printf("%016llx", (unsigned long long int)this->fletcher64());
}

void Array::print_checksum(const char *prefix, bool serial) const {
  auto log = m_impl->grid->ctx()->log();

  log->message(1, "%s%s: %s\n", prefix, m_impl->name.c_str(), checksum(serial).c_str());
}

//! \brief View a 2D vector field using existing PETSc viewers.
void Array::view(std::vector<std::shared_ptr<petsc::Viewer> > viewers) const {
  PetscErrorCode ierr;

  if (ndims() == 3) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "cannot 'view' a 3D field '%s'",
                                  get_name().c_str());
  }

  // Get the dof=1, stencil_width=0 DMDA (components are always scalar
  // and we just need a global Vec):
  auto da2 = m_impl->grid->get_dm(1, 0);

  petsc::TemporaryGlobalVec tmp(da2);

  for (unsigned int i = 0; i < ndof(); ++i) {
    std::string
      long_name           = m_impl->metadata[i]["long_name"],
      units               = m_impl->metadata[i]["units"],
      output_units = m_impl->metadata[i]["output_units"],
      title               = pism::printf("%s (%s)",
                                         long_name.c_str(),
                                         output_units.c_str());

    PetscViewer v = *viewers[i].get();

    PetscDraw draw = NULL;
    ierr = PetscViewerDrawGetDraw(v, 0, &draw);
    PISM_CHK(ierr, "PetscViewerDrawGetDraw");

    ierr = PetscDrawSetTitle(draw, title.c_str());
    PISM_CHK(ierr, "PetscDrawSetTitle");

    get_dof(da2, tmp, i);

    convert_vec(tmp, m_impl->metadata[i].unit_system(),
                units, output_units);

    double bounds[2] = {0.0, 0.0};
    ierr = VecMin(tmp, NULL, &bounds[0]); PISM_CHK(ierr, "VecMin");
    ierr = VecMax(tmp, NULL, &bounds[1]); PISM_CHK(ierr, "VecMax");

    if (bounds[0] == bounds[1]) {
      bounds[0] = -1.0;
      bounds[1] =  1.0;
    }

    ierr = PetscViewerDrawSetBounds(v, 1, bounds);
    PISM_CHK(ierr, "PetscViewerDrawSetBounds");

    ierr = VecView(tmp, v);
    PISM_CHK(ierr, "VecView");
  }
}

} // end of namespace array

void convert_vec(petsc::Vec &v, units::System::Ptr system,
                 const std::string &spec1, const std::string &spec2) {
  units::Converter c(system, spec1, spec2);

  // has to be a PetscInt because of the VecGetLocalSize() call
  PetscInt data_size = 0;
  PetscErrorCode ierr = VecGetLocalSize(v, &data_size);
  PISM_CHK(ierr, "VecGetLocalSize");

  petsc::VecArray data(v);
  c.convert_doubles(data.get(), data_size);
}

} // end of namespace pism
