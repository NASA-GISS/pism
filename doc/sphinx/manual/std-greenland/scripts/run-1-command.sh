#!/bin/bash

mpiexec -n 8 pism \
  -i pism_Greenland_5km_v1.1.nc -bootstrap -grid.registration corner \
  -dx 20km -dy 20km -Mz 101 -Mbz 11 -z_spacing equal -Lz 4000 -Lbz 2000 \
  -skip -skip_max 10 -grid.recompute_longitude_and_latitude false -ys \
  -10000 -ye 0 -surface given -surface_given_file pism_Greenland_5km_v1.1.nc \
  -front_retreat_file pism_Greenland_5km_v1.1.nc -sia_e 3.0 \
  -ts_file ts_g20km_10ka.nc -ts_times -10000:yearly:0 \
  -extra_file ex_g20km_10ka.nc -extra_times -10000:100:0 \
  -extra_vars diffusivity,temppabase,tempicethk_basal,bmelt,tillwat,velsurf_mag,mask,thk,topg,usurf \
  -o g20km_10ka.nc
