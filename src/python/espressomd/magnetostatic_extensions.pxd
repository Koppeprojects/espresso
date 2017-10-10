#
# Copyright (C) 2013,2014,2015,2016 The ESPResSo project
#
# This file is part of ESPResSo.
#
# ESPResSo is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ESPResSo is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Handling of electrostatics

from __future__ import print_function, absolute_import
include "myconfig.pxi"
from espressomd.system cimport *
from espressomd.utils cimport *

IF DIPOLES == 1:

    cdef extern from "mdlc_correction.hpp":
        ctypedef struct dlc_struct "DLC_struct":
            double maxPWerror
            double gap_size
            double far_cut

        int mdlc_set_params(double maxPWerror, double gap_size, double far_cut)

        # links intern C-struct with python object
        cdef extern dlc_struct dlc_params
