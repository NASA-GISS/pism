#ifndef _CONNECTEDCOMPONENTS_H_
#define _CONNECTEDCOMPONENTS_H_

#include <vector>
#include <map>

#include "pism/util/IceGrid.hh"
#include "pism/util/iceModelVec.hh"
#include "pism/util/petscwrappers/Vec.hh"

namespace pism {

typedef std::vector<double> RunVec;
typedef std::map<std::string, RunVec > VecList;
typedef std::vector<IceModelVec*> FieldVec;
typedef std::vector<const IceModelVec*> ConstFieldVec;

class ConnectedComponentsBase {
public:
  ConnectedComponentsBase(IceGrid::ConstPtr g);
  ~ConnectedComponentsBase();

private:
  void resizeLists(VecList &lists, const int new_length);
  void run_union(RunVec &parents, int run1, int run2);

protected:
  const IceGrid::ConstPtr m_grid;
  IceModelVec2Int m_mask_run;
  void check_cell(const int i, const int j,
                  const bool isWest, const bool isSouth, const int mask_w, const int mask_s,
                  int &run_number, VecList &lists, unsigned int &max_items);
  int trackParentRun(int run, const RunVec &parents);
  virtual void init_VecList(VecList &lists, const unsigned int length);
  virtual void startNewRun(const int i, const int j, int &run_number, int &parent, VecList &lists);
  virtual void continueRun(const int i, const int j, int &run_number, VecList &lists);
  virtual void mergeRuns(const int run_number, const int run_south, VecList &lists);
  virtual void compute_runs(int &run_number, VecList &lists, unsigned int &max_items) = 0;
  virtual bool ForegroundCond(const int i, const int j) const = 0;
};

class ConnectedComponents : public ConnectedComponentsBase {
public:
  ConnectedComponents(IceGrid::ConstPtr g);
  ~ConnectedComponents();

protected:
  FieldVec m_masks;
  ConstFieldVec m_fields;
  const int m_i_local_first, m_i_local_last, m_j_local_first, m_j_local_last,
            m_i_global_first, m_i_global_last, m_j_global_first, m_j_global_last;

  void compute_runs(int &run_number, VecList &lists, unsigned int &max_items);
  bool updateRunsAtBoundaries(VecList &lists);
  void updateGhosts(FieldVec &in);
  void addFieldVecAccessList(FieldVec &field, IceModelVec::AccessList &list);
  void addFieldVecAccessList(ConstFieldVec &field, IceModelVec::AccessList &list);
  virtual void labelMask(int run_number, const VecList &lists);
  virtual void treatInnerMargin(const int i, const int j,
                                const bool isNorth, const bool isEast, const bool isSouth, const bool isWest,
                                VecList &lists, bool &changed) {};
};

class ConnectedComponentsSerial : public ConnectedComponentsBase {
public:
  ConnectedComponentsSerial(IceGrid::ConstPtr g);
  ~ConnectedComponentsSerial();

protected:
  petsc::Vec::Ptr m_mask_run_vec_p0;
  petsc::VecArray2D *m_mask_run_p0_ptr;

  void compute_runs(int &run_number, VecList &lists, unsigned int &max_items);
};


class SinkCC : public ConnectedComponents {
public:
  SinkCC(IceGrid::ConstPtr g);
  ~SinkCC();

private:
  void setRunSink(int run, RunVec &parents);

protected:
  virtual bool SinkCond(const int i, const int j);
  virtual void treatInnerMargin(const int i, const int j,
                                const bool isNorth, const bool isEast, const bool isSouth, const bool isWest,
                                VecList &lists, bool &changed);
  virtual void startNewRun(const int i, const int j, int &run_number, int &parent, VecList &lists);
  virtual void continueRun(const int i, const int j, int &run_number, VecList &lists);
};

class MaskCC : public SinkCC {
public:
  MaskCC(IceGrid::ConstPtr g);
  ~MaskCC();
  void compute_mask(IceModelVec2Int &mask);

protected:
  virtual bool ForegroundCond(const int i, const int j) const;

private:
  void labelOutMask(const int run_number, const VecList &lists, IceModelVec2Int &result);
};


template <class CC>
class ValidCC : public CC {
public:
  ValidCC(IceGrid::ConstPtr g);
  ~ValidCC();

private:
  void setRunValid(int run, VecList &lists);

protected:
  IceModelVec2Int m_mask_validity;
  virtual void init_VecList(VecList &lists, const unsigned int length);
  virtual void treatInnerMargin(const int i, const int j,
                                const bool isNorth, const bool isEast, const bool isSouth, const bool isWest,
                                VecList &lists, bool &changed);
  virtual void startNewRun(const int i, const int j, int &run_number, int &parent, VecList &lists);
  virtual void continueRun(const int i, const int j, int &run_number, VecList &lists);
  virtual void mergeRuns(const int run_number, const int run_south, VecList &lists);
  virtual void labelMask(int run_number, const VecList &lists);
};

template <class CC>
ValidCC<CC>::ValidCC(IceGrid::ConstPtr g)
  :CC(g) {
  m_mask_validity.create(CC::m_grid, "mask_validity", WITH_GHOSTS, 1);
  CC::m_masks.push_back(&m_mask_validity);
}

template <class CC>
ValidCC<CC>::~ValidCC() {
  //empty
}

template <class CC>
void ValidCC<CC>::init_VecList(VecList &lists, const unsigned int size) {
  CC::init_VecList(lists, size);

  RunVec valid_list(size);
  lists["valid"] = valid_list;

  for (unsigned int k = 0; k < 2; ++k) {
    lists["valid"][k] = 1;
  }
}

template <class CC>
void ValidCC<CC>::setRunValid(int run, VecList &lists) {
  if ((run == 0) or (run == 1)) {
    return;
  }

  run = CC::trackParentRun(run, lists["parents"]);
  if (run != 1) {
    lists["valid"][run] = 1;
  }
}

template <class CC>
void ValidCC<CC>::treatInnerMargin(const int i, const int j,
                                       const bool isNorth, const bool isEast, const bool isSouth, const bool isWest,
                                       VecList &lists, bool &changed) {
  CC::treatInnerMargin(i, j, isNorth, isEast, isSouth, isWest, lists, changed);

  const int run = CC::m_mask_run.as_int(i, j);
  if (run > 1) {
    //Lake at inner boundary
    const bool isValid = (lists["valid"][run] > 0);
    if (not isValid) {
      //Lake at this side is not labeled as valid
      StarStencil<int> mask_isValid_star = m_mask_validity.int_star(i, j);

      bool WestValid  = (isWest  and (mask_isValid_star.w == 1)),
           EastValid  = (isEast  and (mask_isValid_star.e == 1)),
           SouthValid = (isSouth and (mask_isValid_star.s == 1)),
           NorthValid = (isNorth and (mask_isValid_star.n == 1));

      if (WestValid or EastValid or SouthValid or NorthValid) {
        //Lake at other side is not completely covered with ice
        lists["valid"][run] = 1;
        changed = true;
      }
    }
  }
}

template <class CC>
void ValidCC<CC>::startNewRun(const int i, const int j, int &run_number, int &parent, VecList &lists) {
  CC::startNewRun(i, j, run_number, parent, lists);
  const bool isValid = (m_mask_validity(i, j) > 0);
  lists["valid"][run_number] = isValid ? 1 : 0;
}

template <class CC>
void ValidCC<CC>::continueRun(const int i, const int j, int &run_number, VecList &lists) {
  CC::continueRun(i, j, run_number, lists);
  const bool isValid = (m_mask_validity(i, j) > 0);
  if (isValid) {
    setRunValid(run_number, lists);
  }
}

template <class CC>
void ValidCC<CC>::mergeRuns(const int run_number, const int run_south, VecList &lists) {
  CC::mergeRuns(run_number, run_south, lists);
  const bool isValid = (lists["valid"][run_number] > 0);
  if (isValid) {
    setRunValid(run_number, lists);
  }
}

template <class CC>
void ValidCC<CC>::labelMask(int run_number, const VecList &lists) {
  IceModelVec::AccessList list;
  CC::addFieldVecAccessList(CC::m_masks, list);

  const RunVec &i_vec = lists.find("i")->second,
               &j_vec = lists.find("j")->second,
               &len_vec   = lists.find("lengths")->second,
               &parents   = lists.find("parents")->second,
               &valid_vec = lists.find("valid")->second;

  for (int k = 0; k <= run_number; ++k) {
    const int label = CC::trackParentRun(k, parents);
    const int label_valid = valid_vec[label];
    for (unsigned int n = 0; n < len_vec[k]; ++n) {
      const int i = i_vec[k] + n, j = j_vec[k];
      CC::m_mask_run(i, j) = label;
      m_mask_validity(i, j) = label_valid;
    }
  }
}




template<class CC>
class FillingAlgCC : public CC {
public:
  FillingAlgCC(IceGrid::ConstPtr g, const double drho, const IceModelVec2S &bed, const IceModelVec2S &thk, const double fill_value);
  ~FillingAlgCC();
protected:
  double m_drho, m_fill_value;
  const IceModelVec2S *m_bed, *m_thk;
  virtual bool ForegroundCond(const double bed, const double thk, const int mask, const double Level, const double Offset) const;
};

template<class CC>
FillingAlgCC<CC>::FillingAlgCC(IceGrid::ConstPtr g, const double drho, const IceModelVec2S &bed, const IceModelVec2S &thk, const double fill_value)
  : CC(g), m_drho(drho), m_bed(&bed), m_thk(&thk), m_fill_value(fill_value) {
  CC::m_fields.push_back(m_bed);
  CC::m_fields.push_back(m_thk);
}

template<class CC>
FillingAlgCC<CC>::~FillingAlgCC() {
  //empty
}

template<class CC>
bool FillingAlgCC<CC>::ForegroundCond(const double bed, const double thk, const int mask, const double Level, const double Offset) const {
  if (mask > 0) {
    return true;
  }

  if (Level == m_fill_value) {
    return true;
  }

  double level = Level;
  if (Offset != m_fill_value) {
    level += Offset;
  }

  return ((bed + (m_drho * thk)) < level);
}


class FilterExpansionCC : public ValidCC<ConnectedComponents> {
public:
  FilterExpansionCC(IceGrid::ConstPtr g, const double fill_value, const IceModelVec2S &bed, const IceModelVec2S &water_level);
  ~FilterExpansionCC();
  void filter_ext(const IceModelVec2S &current_level, const IceModelVec2S &target_level, IceModelVec2Int &mask, IceModelVec2S &min_basin, IceModelVec2S &max_water_level);
  void filter_ext2(const IceModelVec2S &current_level, const IceModelVec2S &target_level, IceModelVec2Int &mask, IceModelVec2S &min_basin, IceModelVec2S &max_water_level);

protected:
  virtual void init_VecList(VecList &lists, const unsigned int length);
  virtual void labelMask(int run_number, const VecList &lists);
  virtual void treatInnerMargin(const int i, const int j,
                                const bool isNorth, const bool isEast, const bool isSouth, const bool isWest,
                                VecList &lists, bool &changed);
  virtual void startNewRun(const int i, const int j, int &run_number, int &parent, VecList &lists);
  virtual void continueRun(const int i, const int j, int &run_number, VecList &lists);
  virtual bool ForegroundCond(const int i, const int j) const;

private:
  const double m_fill_value;
  const IceModelVec2S *m_bed, *m_water_level;
  IceModelVec2S m_min_bed, m_max_wl;

  void setRunMinBed(double level, int run, VecList &lists);
  void setRunMaxWl(double level, int run, VecList &lists);
  void labelMap(const int run_number, const VecList &lists, IceModelVec2Int &mask, IceModelVec2S &min_bed, IceModelVec2S &max_wl);
  void labelMap2(const int run_number, const VecList &lists, IceModelVec2Int &mask, IceModelVec2S &min_bed, IceModelVec2S &max_wl);
  void prepare_mask(const IceModelVec2S &current_level, const IceModelVec2S &target_level);
  void set_mask_validity(const int n_filter);

  inline bool ForegroundCond(const int mask) const {
    return (mask > 1);
  }

  inline bool isLake(const double level) {
    return (level != m_fill_value);
  }
};

} //namespace pism

#endif
