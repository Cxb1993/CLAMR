The CLAMR code is a cell-based adaptive mesh refinement (AMR) mini-app developed
as a testbed for hybrid algorithm development using MPI and OpenCL GPU code. 

The CLAMR code is open-sourced under its LANL copyright
/*
 *  Copyright (c) 2011-2012, Los Alamos National Security, LLC.
 *  All rights Reserved.
 *
 *  Copyright 2011-2012. Los Alamos National Security, LLC. This software was produced 
 *  under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National 
 *  Laboratory (LANL), which is operated by Los Alamos National Security, LLC 
 *  for the U.S. Department of Energy. The U.S. Government has rights to use, 
 *  reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR LOS 
 *  ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR 
 *  ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is modified
 *  to produce derivative works, such modified software should be clearly marked,
 *  so as not to confuse it with the version available from LANL.
 *
 *  See LICENSE file for full copyright 
 *  
 */

Contributions to CLAMR are welcomed as long as they do not substantially change
the nature of the code.

To build the CLAMR executables

./configure

use ./configure --help to see all the options available. The most common is 
 --disable-opengl: to disable the real-time graphics

make

Two executables are currently built

clamr_gpuonly: Calls the GPU versions of each call. Option to check the results
against the cpu calls

clamr_cpuonly: Calls the CPU versions of each call. Option to check the results
against the cpu calls

clamr_gpucheck: Calls the GPU and CPU versions of each call and checks the results
against each other

clamr_checkall: Calls the GPU, CPU, MPI and GPU/MPI versions of each call and checks
the results against each other.

More executables are planned

Currently the executables run only on NVIDIA GPUs. Fixing the kernels to run on
ATI GPUs is of great interest

The refinement smoothing is only added for the GPU and CPU versions -- it still needs
to be added to the MPI versions

Many other limitations exist -- coarsening has not been implemented and boundary
conditions need some more work

Current performance shows about a 50x speedup on the GPU versus the CPU using NVIDIA
Tesla 2050s and AMD CPUs

See the PAPERS file for a list of publications related to the CLAMR code (papers.bib for 
bibtex format)
