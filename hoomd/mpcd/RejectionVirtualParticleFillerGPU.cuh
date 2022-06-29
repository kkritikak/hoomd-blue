// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: mphoward

#ifndef MPCD_REJECTION_VIRTUAL_PARTICLE_FILLER_GPU_CUH_
#define MPCD_REJECTION_VIRTUAL_PARTICLE_FILLER_GPU_CUH_

/*!
 * \file mpcd/RejectionVirtualParticleFillerGPU.cuh
 * \brief Declaration of CUDA kernels for mpcd::RejectionVirtualParticleFillerGPU
 */

#include <cuda_runtime.h>
#include <cub/cub.cuh>

#include "hoomd/HOOMDMath.h"
#include "hoomd/BoxDim.h"
#include "ParticleDataUtilities.h"

#include "hoomd/RandomNumbers.h"
#include "hoomd/RNGIdentifiers.h"

namespace mpcd
{
namespace gpu
{

//! Common arguments passed to all geometry filling kernels
struct draw_virtual_particles_args_t
    {
    //! Constructor
    draw_virtual_particles_args_t(Scalar4 *_d_tmp_pos,
                                  Scalar4 *_d_tmp_vel,
                                  bool *_d_track_bounded_particles,
                                  const Scalar3 _lo,
                                  const Scalar3 _hi,
                                  const unsigned int _first_tag,
                                  const Scalar _vel_factor,
                                  const unsigned int _filler_id,
                                  const unsigned int _type,
                                  const unsigned int _N_virt_max,
                                  const unsigned int _timestep,
                                  const unsigned int _seed,
                                  const unsigned int _block_size)
        : d_tmp_pos(_d_tmp_pos), d_tmp_vel(_d_tmp_vel), d_track_bounded_particles(_d_track_bounded_particles),
        lo(_lo), hi(_hi), first_tag(_first_tag), vel_factor(_vel_factor), filler_id(_filler_id), type(_type),
        N_virt_max(_N_virt_max), timestep(_timestep), seed(_seed), block_size(_block_size)
        { }

    Scalar4 *d_tmp_pos;
    Scalar4 *d_tmp_vel;
    bool *d_track_bounded_particles;
    const Scalar3 lo;
    const Scalar3 hi;
    const unsigned int first_tag;
    const Scalar vel_factor;
    const unsigned int filler_id;
    const unsigned int type;
    const unsigned int N_virt_max;
    const unsigned int timestep;
    const unsigned int seed;
    const unsigned int block_size;
    };

// Function declarations
template<class Geometry>
cudaError_t draw_virtual_particles(const draw_virtual_particles_args_t& args, const Geometry& geom);

cudaError_t parallel_tagging(unsigned int *d_tag,
                             const unsigned int first_tag,
                             const unsigned int first_idx,
                             const unsigned int block_size);

cudaError_t check_storage(void *d_storage, size_t reqSize);

cudaError_t compact_data_arrays(Scalar4 *d_in,
                                bool *d_flags,
                                const unsigned int num_items,
                                Scalar4 *d_out,
                                unsigned int *d_num_selected_out);

cudaError_t copy_data(Scalar4 *d_permanent, Scalar4 *d_temp, const unsigned int first_idx);


#ifdef NVCC
namespace kernel
{

//! Kernel to draw virtual particles outside any given geometry
/*!
 * \param d_tmp_pos Temporary positions
 * \param d_tmp_vel Temporary velocities
 * \param d_track_bounded_particles Particle tracking - in/out of given geometry
 * \param lo Left extrema of the sim-box
 * \param hi Right extrema of the sim-box
 * \param first_tag First tag (rng argument)
 * \param vel_factor Scale factor for uniform normal velocities consistent with particle mass / temperature
 * \param filler_id Identifier for the filler (rng argument)
 * \param type Particle type for filling
 * \param N_virt_max Maximum no. of virtual particles that can exist
 * \param timestep Current timestep
 * \param seed User seed for RNG
 *
 * \tparam Geometry type of the confined geometry \a geom
 *
 * \b implementation
 * TODO: Add documentation here after the script is done
 */
template<class Geometry>
__global__ void draw_virtual_particles(Scalar4 *d_tmp_pos,
                                       Scalar4 *d_tmp_vel,
                                       bool *d_track_bounded_particles,
                                       const Scalar3 lo,
                                       const Scalar3 hi,
                                       const unsigned int first_tag,
                                       const Scalar vel_factor,
                                       const unsigned int filler_id,
                                       const unsigned int type,
                                       const unsigned int N_virt_max,
                                       const unsigned int timestep,
                                       const unsigned int seed,
                                       const unsigned int block_size,
                                       const Geometry geom)
    {
    // one thread per particle
    const unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= N_virt_max)
        return;

    // initialize random number generator for positions and velocity
    hoomd::RandomGenerator rng(hoomd::RNGIdentifier::RejectionFiller, seed, timestep, first_tag+idx, filler_id);
    d_tmp_pos[idx] = make_scalar4(hoomd::UniformDistribution<Scalar>(lo.x, hi.x)(rng),
                                  hoomd::UniformDistribution<Scalar>(lo.y, hi.y)(rng),
                                  hoomd::UniformDistribution<Scalar>(lo.z, hi.z)(rng),
                                  __int_as_scalar(type));

    hoomd::NormalDistribution<Scalar> gen(vel_factor, 0.0);
    Scalar3 vel;
    gen(vel.x, vel.y, rng);
    vel.z = gen(rng);
    d_tmp_vel[idx] = make_scalar4(vel.x,
                                  vel.y,
                                  vel.z,
                                  __int_as_scalar(mpcd::detail::NO_CELL));

    // check if particle is inside/outside the confining geometry
    d_track_bounded_particles[idx] = geom.isOutside(make_scalar3(d_tmp_pos[idx].x,
                                                                  d_tmp_pos[idx].y,
                                                                  d_tmp_pos[idx].z));
    }

__global__ void parallel_copy(unsigned int *d_compact_indices,
                              Scalar4 *d_permanent_positions,
                              Scalar4 *d_permanent_velocities,
                              Scalar4 *d_tag,
                              Scalar4 *d_temporary_positions,
                              Scalar4 *d_temporary_velocities,
                              const unsigned int first_idx,
                              const unsigned int first_tag,
                              const unsigned int n_virtual,
                              const unsigned int block_size)
    {
    // one thread per virtual particle
    const unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n_virtual)
        return;

    const unsigned int real_idx = first_idx + idx;
    d_permanent_positions[real_idx] = d_temporary_positions[idx];
    d_permanent_velocities[real_idx] = d_temporary_velocities[idx];
    d_tag[real_idx] = first_tag + idx;
    }

} // end namespace kernel

/*!
 * \param args Common arguments for all geometries
 * \param geom Confined geometry
 *
 * \tparam Geometry type of the confined geometry \a geom
 *
 * \sa mpcd::gpu::kernel::draw_virtual_particles
 */
template<class Geometry>
cudaError_t draw_virtual_particles(const draw_virtual_particles_args_t& args, const Geometry& geom)
    {
    static unsigned int max_block_size = UINT_MAX;
    if (max_block_size == UINT_MAX)
        {
        cudaFuncAttributes attr;
        cudaFuncGetAttributes(&attr, (const void*)mpcd::gpu::kernel::draw_virtual_particles<Geometry>);
        max_block_size = attr.maxThreadsPerBlock;
        }

    unsigned int run_block_size = min(args.block_size, max_block_size);
    dim3 grid(args.N_virt_max / run_block_size + 1);
    mpcd::gpu::kernel::draw_virtual_particles<Geometry><<<grid, run_block_size>>>(args.d_tmp_pos, args.d_tmp_vel, args.d_track_bounded_particles,
    args.lo, args.hi, args.first_tag, args.vel_factor, args.filler_id, args.type, args.N_virt_max, args.timestep, args.seed, args.block_size, geom);

    return cudaSuccess;
    }

cudaError_t check_storage(void *d_storage, size_t req_size)
    {
    const static float resize_factor = 1.1;
    size_t storage_size = sizeof(d_storage);

    if (storage_size == NULL)
        cudaMalloc(&d_storage, req_size);

    else if (req_size > storage_size)
        {
        while (req_size > storage_size)
            {
            storage_size = ((unsigned int) (((float) storage_size) * 1.1)) + 1;
            }
        cudaFree(&d_storage);
        cudaMalloc(&d_storage, storage_size);
        }
    return cudaSuccess;
    }


cudaError_t compact_indices(bool *d_flags,
                            const unsigned int num_items,
                            unsigned int *d_out,
                            unsigned int *d_num_selected_out,
                            void *d_temp_storage,
                            size_t temp_storage_bytes)
    {
    // Determine temporary device storage requirements
    cub::DeviceSelect::Flagged(d_temp_storage, temp_storage_bytes, cub::CountingInputIterator<int> itr, d_flags,
                               d_out, d_num_selected_out, num_items);
    // Check if temporary storage is enough
    size_t req_size = num_items * sizeof(unsigned int)
    check_storage(d_temp_storage, req_size)
    // Run selection
    cub::DeviceSelect::Flagged(d_temp_storage, temp_storage_bytes, cub::CountingInputIterator<int> itr, d_flags,
                               d_out, d_num_selected_out, num_items);
    return cudaSuccess;
    }


cudaError_t parallel_copy(unsigned int *d_compact_indices,
                          Scalar4 *d_permanent_positions,
                          Scalar4 *d_permanent_velocities,
                          Scalar4 *d_permanent_tags,
                          Scalar4 *d_temporary_positions,
                          Scalar4 *d_temporary_velocities,
                          const unsigned int first_idx,
                          const unsigned int first_tag,
                          const unsigned int n_virtual,
                          const unsigned int block_size)
    {
    static unsigned int max_block_size = UINT_MAX;
    if (max_block_size == UINT_MAX)
        {
        cudaFuncAttributes attr;
        cudaFuncGetAttributes(&attr, (const void*)mpcd::gpu::kernel::parallel_copy);
        max_block_size = attr.maxThreadsPerBlock;
        }

    unsigned int run_block_size = min(block_size, max_block_size);
    dim3 grid(nVirt / run_block_size + 1);
    mpcd::gpu::kernel::parallel_copy<<<grid, run_block_size>>>(d_compact_indices, d_permanent_positions,
                                                               d_permanent_velocities, d_permanent_tags,
                                                               d_temporary_positions, d_temporary_velocities,
                                                               first_idx, first_tag, n_virtual, block_size);

    return cudaSuccess;
    }


#endif // NVCC

} // end namespace gpu
} // end namespace mpcd

#endif // MPCD_REJECTION_VIRTUAL_PARTICLE_FILLER_GPU_CUH_
