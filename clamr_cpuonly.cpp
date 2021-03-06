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
 *  Additionally, redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Los Alamos National Security, LLC, Los Alamos 
 *       National Laboratory, LANL, the U.S. Government, nor the names of its 
 *       contributors may be used to endorse or promote products derived from 
 *       this software without specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE LOS ALAMOS NATIONAL SECURITY, LLC AND 
 *  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT 
 *  NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL
 *  SECURITY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 *  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 *  CLAMR -- LA-CC-11-094
 *  This research code is being developed as part of the 
 *  2011 X Division Summer Workshop for the express purpose
 *  of a collaborative code for development of ideas in
 *  the implementation of AMR codes for Exascale platforms
 *  
 *  AMR implementation of the Wave code previously developed
 *  as a demonstration code for regular grids on Exascale platforms
 *  as part of the Supercomputing Challenge and Los Alamos 
 *  National Laboratory
 *  
 *  Authors: Bob Robey       XCP-2   brobey@lanl.gov
 *           Neal Davis              davis68@lanl.gov, davis68@illinois.edu
 *           David Nicholaeff        dnic@lanl.gov, mtrxknight@aol.com
 *           Dennis Trujillo         dptrujillo@lanl.gov, dptru10@gmail.com
 * 
 */

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include "display.h"
#ifdef HAVE_OPENCL
#include "ezcl/ezcl.h"
#endif
#include "input.h"
#include "mesh/mesh.h"
#include "mesh/partition.h"
#include "state.h"
#include "timer/timer.h"
#include "memstats/memstats.h"
#include "crux/crux.h"

#ifndef DEBUG 
#define DEBUG 0
#endif
#undef DEBUG_RESTORE_VALS

#define MIN3(x,y,z) ( min( min(x,y), z) )

static int do_gpu_calc = 0;
static int do_cpu_calc = 1;

typedef unsigned int uint;

#ifdef HAVE_GRAPHICS
static double circle_radius=-1.0;
#endif

static int view_mode = 0;

#ifdef FULL_PRECISION
   void (*set_cell_coordinates)(double *, double *, double *, double *) = &set_cell_coordinates_double;
   void (*set_cell_data)(double *) = &set_cell_data_double;
#else
   void (*set_cell_coordinates)(float *, float *, float *, float *) = &set_cell_coordinates_float;
   void (*set_cell_data)(float *) = &set_cell_data_float;
#endif

void store_crux_data(Crux *crux, int ncycle);
void restore_crux_data_bootstrap(Crux *crux, char *restart_file, int rollback_counter);
void restore_crux_data(Crux *crux);

bool        restart,        //  Flag to start from a back up file; init in input.cpp::parseInput().
            verbose,        //  Flag for verbose command-line output; init in input.cpp::parseInput().
            localStencil,   //  Flag for use of local stencil; init in input.cpp::parseInput().
            outline;        //  Flag for drawing outlines of cells; init in input.cpp::parseInput().
int         outputInterval, //  Periodicity of output; init in input.cpp::parseInput().
            crux_type,      //  Type of checkpoint/restart -- CRUX_NONE, CRUX_IN_MEMORY, CRUX_DISK;
                            //  init in input.cpp::parseInput().
            enhanced_precision_sum,//  Flag for enhanced precision sum (default true); init in input.cpp::parseInput().
            lttrace_on,     //  Flag to turn on logical time trace package;
            do_quo_setup,   //  Flag to turn on quo dynamic scheduling policies package;
            levmx,          //  Maximum number of refinement levels; init in input.cpp::parseInput().
            nx,             //  x-resolution of coarse grid; init in input.cpp::parseInput().
            ny,             //  y-resolution of coarse grid; init in input.cpp::parseInput().
            niter,          //  Maximum iterations; init in input.cpp::parseInput().
            graphic_outputInterval, // Periodicity of graphic output that is saved; init in input.cpp::parseInput()
            checkpoint_outputInterval, // Periodicity of checkpoint output that is saved; init in input.cpp::parseInput()
            num_of_rollback_states,// Maximum number of rollback states to maintain; init in input.cpp::parseInput()
            backup_file_num,//  Backup file number to restart simulation from; init in input.cpp::parseInput()
            numpe,          //  
            ndim    = 2;    //  Dimensionality of problem (2 or 3).
double      upper_mass_diff_percentage; //  Flag for the allowed pecentage difference to the total
                                        //  mass per output intervals; init in input.cpp::parseInput().
char *restart_file;

static int it = 0;

enum partition_method initial_order,  //  Initial order of mesh.
                      cycle_reorder;  //  Order of mesh every cycle.
static Mesh       *mesh;           //  Object containing mesh information
static State      *state;          //  Object containing state information corresponding to mesh
static Crux       *crux;           //  Object containing checkpoint/restart information

static real_t circ_radius = 0.0;
static int next_cp_cycle = 0;
static int next_graphics_cycle = 0;

//  Set up timing information.
static struct timeval tstart;

static double  H_sum_initial = 0.0;
static double  cpu_time_graphics = 0.0;

static int     ncycle  = 0;
static double  simTime = 0.0;
static double  deltaT = 0.0;
char total_sim_time_log[] = {"total_execution_time.log"};
struct timeval total_exec;

int main(int argc, char **argv) {

    // Needed for code to compile correctly on the Mac
   int mype=0;
   int numpe=-1;

   //  Process command-line arguments, if any.
   parseInput(argc, argv);

   struct timeval tstart_setup;
   cpu_timer_start(&tstart_setup);
   
   numpe = 16;

   crux = new Crux(crux_type, num_of_rollback_states, restart);
   
   circ_radius = 6.0;
   //  Scale the circle appropriately for the mesh size.
   circ_radius = circ_radius * (real_t) nx / 128.0;
   int boundary = 1;
   int parallel_in = 0;
   
   if (restart){
      restore_crux_data_bootstrap(crux, restart_file, 0);
      mesh  = new Mesh(nx, ny, levmx, ndim, boundary, parallel_in, do_gpu_calc);
      mesh->init(nx, ny, circ_radius, initial_order, do_gpu_calc);

      state = new State(mesh);
      restore_crux_data(crux);
      mesh->proc.resize(mesh->ncells);
      mesh->calc_distribution(numpe);
   } else {
      mesh  = new Mesh(nx, ny, levmx, ndim, boundary, parallel_in, do_gpu_calc);
      if (DEBUG) {
         //if (mype == 0) mesh->print();

         char filename[10];
         sprintf(filename,"out%1d",mype);
         mesh->fp=fopen(filename,"w");

         //mesh->print_local();
      } 

      mesh->init(nx, ny, circ_radius, initial_order, do_gpu_calc);
      state = new State(mesh);
      state->init(do_gpu_calc);
      mesh->proc.resize(mesh->ncells);
      mesh->calc_distribution(numpe);
      state->fill_circle(circ_radius, 100.0, 7.0);
   }
   if (graphic_outputInterval > niter) next_graphics_cycle = graphic_outputInterval;
   if (checkpoint_outputInterval > niter) next_cp_cycle = checkpoint_outputInterval;

   size_t &ncells = mesh->ncells;
   mesh->nlft = NULL;
   mesh->nrht = NULL;
   mesh->nbot = NULL;
   mesh->ntop = NULL;

   //  Kahan-type enhanced precision sum implementation.
   double H_sum = state->mass_sum(enhanced_precision_sum);
   printf ("Mass of initialized cells equal to %14.12lg\n", H_sum);
   H_sum_initial = H_sum;

   double cpu_time_main_setup = cpu_timer_stop(tstart_setup);
   state->parallel_timer_output(numpe,mype,"CPU:  setup time               time was",cpu_time_main_setup);

   long long mem_used = memstats_memused();
   if (mem_used > 0) {
      printf("Memory used      in startup %lld kB\n",mem_used);
      printf("Memory peak      in startup %lld kB\n",memstats_mempeak());
      printf("Memory free      at startup %lld kB\n",memstats_memfree());
      printf("Memory available at startup %lld kB\n",memstats_memtotal());
   }

   if (ncycle != 0){
      printf("Iteration %3d timestep %lf Sim Time %lf cells %ld Mass Sum %14.12lg\n",
         ncycle, deltaT, simTime, ncells, H_sum);
   } else {
      printf("Iteration   0 timestep      n/a Sim Time      0.0 cells %ld Mass Sum %14.12lg\n", ncells, H_sum);
   }

   mesh->cpu_calc_neigh_counter=0;
   mesh->cpu_time_calc_neighbors=0.0;
   mesh->cpu_rezone_counter=0;
   mesh->cpu_time_rezone_all=0.0;
   mesh->cpu_refine_smooth_counter=0;

   //  Set up grid.
#ifdef GRAPHICS_OUTPUT
   mesh->write_grid(n);
#endif

   set_mysize(ncells);
   set_window((float)mesh->xmin, (float)mesh->xmax, (float)mesh->ymin, (float)mesh->ymax);
   set_outline((int)outline);
   set_cell_coordinates(&mesh->x[0], &mesh->dx[0], &mesh->y[0], &mesh->dy[0]);
   set_cell_data(&state->H[0]);
   set_cell_proc(&mesh->proc[0]);
   set_viewmode(view_mode);

   if (ncycle == next_graphics_cycle){
      init_graphics_output();
      write_graphics_info(0,0,0.0,0,0);
      next_graphics_cycle += graphic_outputInterval;
   }

#ifdef HAVE_GRAPHICS
   set_circle_radius(circle_radius);
   init_display(&argc, argv, "Shallow Water", mype);
   draw_scene();
   //if (verbose) sleep(5);
   sleep(2);

   //  Set flag to show mesh results rather than domain decomposition.
   view_mode = 1;

   //  Clear superposition of circle on grid output.
   circle_radius = -1.0;
#endif

   if (ncycle == next_cp_cycle) store_crux_data(crux, ncycle); 

   cpu_timer_start(&tstart);
#ifdef HAVE_GRAPHICS
   set_idle_function(&do_calc);
   start_main_loop();
#else
   for (int it = ncycle; it < 10000000; it++) {
      do_calc();
   }
#endif
   
   return 0;
}

extern "C" void do_calc(void)
{  double g     = 9.80;
   double sigma = 0.95;
   int icount, jcount;
   static int rollback_attempt = 0;
   static double total_program_time = 0;

   //  Initialize state variables for GPU calculation.
   size_t &ncells    = mesh->ncells;

   vector<int>     mpot;
   
   //size_t old_ncells = ncells;
   size_t new_ncells = 0;
   double H_sum = -1.0;

   //  Main loop.
   int endcycle = MIN3(niter, next_cp_cycle, next_graphics_cycle);

   for (int nburst = ncycle % outputInterval; nburst < outputInterval && ncycle < endcycle; nburst++, ncycle++) {

      //old_ncells = ncells;

      //  Calculate the real time step for the current discrete time step.
      deltaT = state->set_timestep(g, sigma);
      simTime += deltaT;
      
      if (mesh->nlft == NULL) mesh->calc_neighbors();

      mesh->partition_measure();

      // Currently not working -- may need to be earlier?
      //if (do_cpu_calc && ! mesh->have_boundary) {
      //  state->add_boundary_cells(mesh);
      //}

      // Apply BCs is currently done as first part of gpu_finite_difference and so comparison won't work here

      //  Execute main kernel
      state->calc_finite_difference(deltaT);
      
      //  Size of arrays gets reduced to just the real cells in this call for have_boundary = 0
      state->remove_boundary_cells();
      
      mpot.resize(ncells);
      new_ncells = state->calc_refine_potential(mpot, icount, jcount);

      //  Resize the mesh, inserting cells where refinement is necessary.

      state->rezone_all(icount, jcount, mpot);

      // Clear does not delete mpot, so have to swap with an empty vector to get
      // it to delete the mpot memory. This is all to avoid valgrind from showing
      // it as a reachable memory leak
      //mpot.clear(); 
      vector<int>().swap(mpot);

      mesh->ncells = new_ncells;
      ncells = new_ncells;

      mesh->proc.resize(ncells);
      if (icount)
      {  vector<int> index(ncells);
         mesh->partition_cells(numpe, index, cycle_reorder);
         state->state_reorder(index);
         state->memory_reset_ptrs();
      }
      
      mesh->ncells = ncells;
   }
      
   H_sum = state->mass_sum(enhanced_precision_sum);

   int error_status = STATUS_OK;

   if (isnan(H_sum)) {
      printf("Got a NAN on cycle %d\n",ncycle);
      error_status = STATUS_NAN;
   }

   double percent_mass_diff = fabs(H_sum - H_sum_initial)/H_sum_initial * 100.0;
   if (percent_mass_diff >= upper_mass_diff_percentage) {
      printf("Mass difference outside of acceptable range on cycle %d percent_mass_diff %lg upper limit %lg\n",ncycle,percent_mass_diff, upper_mass_diff_percentage);
      error_status = STATUS_MASS_LOSS;
   }

   if (error_status != STATUS_OK){
      if (crux_type != CRUX_NONE) {

         rollback_attempt++;
         if (rollback_attempt > num_of_rollback_states) {
            printf("Can not recover from error from back up files. Killing program...\n");
            total_program_time = cpu_timer_stop(total_exec);
            FILE *fp = fopen(total_sim_time_log,"w");
            fprintf(fp,"The total execution time of the program before failure was %g seconds\n", total_program_time);
            fclose(fp);
            state->print_failure_log(ncycle, simTime, H_sum_initial, H_sum, percent_mass_diff, true);
            exit(-1);
         }

         if (graphic_outputInterval <= niter){
            mesh->calc_spatial_coordinates(0);
            set_mysize(ncells);
            set_viewmode(view_mode);
            set_cell_coordinates(&mesh->x[0], &mesh->dx[0], &mesh->y[0], &mesh->dy[0]);
            set_cell_data(&state->H[0]);
            set_cell_proc(&mesh->proc[0]);
            write_graphics_info(ncycle/graphic_outputInterval,ncycle,simTime,1,rollback_attempt);
         }

         if((ncycle - (rollback_attempt)*checkpoint_outputInterval) < 0){
            printf("Rolling simulation back to to ncycle 0\n");
         }
         else{
            printf("Rolling simulation back to to ncycle %d\n", ncycle - (rollback_attempt*checkpoint_outputInterval));
         }

         state->print_rollback_log(ncycle, simTime, H_sum_initial, H_sum, percent_mass_diff, rollback_attempt, num_of_rollback_states, error_status);

         int rollback_num = crux->get_rollback_number();

         restore_crux_data_bootstrap(crux, NULL, rollback_num);
         mesh->terminate();
         state->terminate();
         restore_crux_data(crux);


      } else {
         printf("failure.log has been created\n");
         state->print_failure_log(ncycle, simTime, H_sum_initial, H_sum, percent_mass_diff, true);
         exit(-1);
      }
   }


   if(ncycle == next_graphics_cycle){
      mesh->calc_spatial_coordinates(0);
      set_mysize(ncells);
      set_viewmode(view_mode);
      set_cell_coordinates(&mesh->x[0], &mesh->dx[0], &mesh->y[0], &mesh->dy[0]);
      set_cell_data(&state->H[0]);
      set_cell_proc(&mesh->proc[0]);
      write_graphics_info(ncycle/graphic_outputInterval,ncycle,simTime,0,0);
      next_graphics_cycle += graphic_outputInterval;
   }

   if (ncycle == next_cp_cycle) store_crux_data(crux, ncycle); 

   if (ncycle % outputInterval == 0) {
      printf("Iteration %3d timestep %lf Sim Time %lf cells %ld Mass Sum %14.12lg Mass Change %12.6lg\n",
         ncycle, deltaT, simTime, ncells, H_sum, H_sum - H_sum_initial);
   }

   struct timeval tstart_cpu;
   cpu_timer_start(&tstart_cpu);

#ifdef HAVE_GRAPHICS
   if(ncycle % outputInterval == 0){
      if(ncycle != next_graphics_cycle){
         mesh->calc_spatial_coordinates(0);

         set_mysize(ncells);
         set_viewmode(view_mode);
         set_cell_coordinates(&mesh->x[0], &mesh->dx[0], &mesh->y[0], &mesh->dy[0]);
         set_cell_data(&state->H[0]);
         set_cell_proc(&mesh->proc[0]);
      }
      set_circle_radius(circle_radius);
      draw_scene();
   }
#endif

   cpu_time_graphics += cpu_timer_stop(tstart_cpu);

   //  Output final results and timing information.
   if (ncycle >= niter) {
      //free_display();
      //state->print();
      
      if(graphic_outputInterval < niter){

         mesh->calc_spatial_coordinates(0);
         set_mysize(ncells);
         set_viewmode(view_mode);
         set_cell_coordinates(&mesh->x[0], &mesh->dx[0], &mesh->y[0], &mesh->dy[0]);
         set_cell_data(&state->H[0]);
         set_cell_proc(&mesh->proc[0]);
         write_graphics_info(ncycle/graphic_outputInterval,ncycle,simTime,0,0);
         next_graphics_cycle += graphic_outputInterval;
      }

      //  Get overall program timing.
      double elapsed_time = cpu_timer_stop(tstart);
      
      long long mem_used = memstats_memused();
      //if (mem_used > 0) {
         printf("Memory used      %lld kB\n",mem_used);
         printf("Memory peak      %lld kB\n",memstats_mempeak());
         printf("Memory free      %lld kB\n",memstats_memfree());
         printf("Memory available %lld kB\n",memstats_memtotal());
      //}
      state->output_timing_info(do_cpu_calc, do_gpu_calc, elapsed_time);
      printf("CPU:  graphics                 time was\t %8.4f\ts\n",     cpu_time_graphics );

      mesh->print_partition_measure();
      mesh->print_calc_neighbor_type();
      mesh->print_partition_type();

      printf("CPU:  rezone frequency                \t %8.4f\tpercent\n",     (double)mesh->get_cpu_rezone_count()/(double)ncycle*100.0 );
      printf("CPU:  calc neigh frequency            \t %8.4f\tpercent\n",     (double)mesh->get_cpu_calc_neigh_count()/(double)ncycle*100.0 );
      printf("CPU:  refine_smooth_iter per rezone   \t %8.4f\t\n",            (double)mesh->get_cpu_refine_smooth_count()/(double)mesh->get_cpu_rezone_count() );

      mesh->terminate();
      state->terminate();

      delete mesh;
      delete state;
      delete crux;

      total_program_time = cpu_timer_stop(total_exec);
      FILE *fp = fopen(total_sim_time_log,"w");
      fprintf(fp,"The total execution time of the program was %g seconds\n", total_program_time);
      fclose(fp);
      exit(0);
   }  //  Complete final output.
   
} // end do_calc

const int CRUX_CLAMR_VERSION = 101;
const int num_int_vals       = 14;
const int num_double_vals    =  5;

void store_crux_data(Crux *crux, int ncycle)
{
   size_t nsize = num_int_vals*sizeof(int) +
                  num_double_vals*sizeof(double);
   nsize += state->get_checkpoint_size();

   crux->store_begin(nsize, ncycle);

   int int_vals[num_int_vals];

   int_vals[ 0] = CRUX_CLAMR_VERSION; // Version number
   int_vals[ 1] = nx;
   int_vals[ 2] = ny;
   int_vals[ 3] = levmx;
   int_vals[ 4] = ndim;
   int_vals[ 5] = outputInterval;
   int_vals[ 6] = enhanced_precision_sum;
   int_vals[ 7] = niter;
   int_vals[ 8] = it;
   int_vals[ 9] = ncycle;
   int_vals[10] = graphic_outputInterval;
   int_vals[11] = checkpoint_outputInterval;
   int_vals[12] = next_cp_cycle;
   int_vals[13] = next_graphics_cycle;

   crux->store_ints(int_vals, num_int_vals);

   double double_vals[num_double_vals];
   double_vals[ 0] = circ_radius;
   double_vals[ 1] = H_sum_initial;
   double_vals[ 2] = simTime;
   double_vals[ 3] = deltaT;
   double_vals[ 4] = upper_mass_diff_percentage;

   crux->store_doubles(double_vals, num_double_vals);

   state->store_checkpoint(crux);

   crux->store_end();

   next_cp_cycle += checkpoint_outputInterval;
}

void restore_crux_data_bootstrap(Crux *crux, char *restart_file, int rollback_counter)
{
   crux->restore_begin(restart_file, rollback_counter);

   int int_vals[num_int_vals];

   crux->restore_ints(int_vals, num_int_vals);

   if (int_vals[ 0] != CRUX_CLAMR_VERSION) {
      printf("CRUX version mismatch for clamr data, version on file is %d, version in code is %d\n",
         int_vals[0], CRUX_CLAMR_VERSION);
      exit(0);
   }
  
   nx                        = int_vals[ 1];
   ny                        = int_vals[ 2];
   levmx                     = int_vals[ 3];
   ndim                      = int_vals[ 4];
   outputInterval            = int_vals[ 5];
   enhanced_precision_sum    = int_vals[ 6];
   niter                     = int_vals[ 7];
   it                        = int_vals[ 8];
   ncycle                    = int_vals[ 9];
   graphic_outputInterval    = int_vals[10];
   checkpoint_outputInterval = int_vals[11];
   next_cp_cycle             = int_vals[12];
   next_graphics_cycle       = int_vals[13];

#ifdef DEBUG_RESTORE_VALS
   if (DEBUG_RESTORE_VALS) {
      const char *int_vals_descriptor[num_int_vals] = {
         "CRUX_CLAMR_VERSION",
         "nx",
         "ny",
         "levmx",
         "ndim",
         "outputInterval",
         "enhanced_precision_sum",
         "niter",
         "it",
         "ncycle",
         "graphic_outputInterval",
         "checkpoint_outputInterval",
         "next_cp_cycle",
         "next_graphics_cycle"
      };
      printf("\n");
      printf("       === Restored bootstrap int_vals ===\n");
      for (int i = 0; i < num_int_vals; i++){
         printf("       %-30s %d\n",int_vals_descriptor[i], int_vals[i]);
      }
      printf("       === Restored bootstrap int_vals ===\n");
      printf("\n");
   }
#endif

   double double_vals[num_double_vals];

   crux->restore_doubles(double_vals, num_double_vals);

   circ_radius                = double_vals[ 0];
   H_sum_initial              = double_vals[ 1];
   simTime                    = double_vals[ 2];
   deltaT                     = double_vals[ 3];
   upper_mass_diff_percentage = double_vals[ 4];

#ifdef DEBUG_RESTORE_VALS
   if (DEBUG_RESTORE_VALS) {
      const char *double_vals_descriptor[num_double_vals] = {
         "circ_radius",
         "H_sum_initial",
         "simTime",
         "deltaT",
         "upper_mass_diff_percentage"
      };
      printf("\n");
      printf("       === Restored bootstrap double_vals ===\n");
      for (int i = 0; i < num_double_vals; i++){
         printf("       %-30s %lg\n",double_vals_descriptor[i], double_vals[i]);
      }
      printf("       === Restored bootstrap double_vals ===\n");
      printf("\n");
   }
#endif
}

void restore_crux_data(Crux *crux)
{
   state->restore_checkpoint(crux);

   crux->restore_end();
}

