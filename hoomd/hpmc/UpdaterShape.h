#ifndef _UPDATER_SHAPE_H
#define _UPDATER_SHAPE_H

#include <numeric>
#include <algorithm>
#include "hoomd/Updater.h"
#include "hoomd/extern/saruprng.h"
#include "IntegratorHPMCMono.h"
#include "hoomd/HOOMDMPI.h"

#include "ShapeUtils.h"
#include "ShapeMoves.h"
#include "hoomd/GSDState.h"

#include <hoomd/extern/pybind/include/pybind11/pybind11.h>

namespace hpmc {


template< typename Shape >
class UpdaterShape  : public Updater
{
public:
    UpdaterShape(   std::shared_ptr<SystemDefinition> sysdef,
                    std::shared_ptr< IntegratorHPMCMono<Shape> > mc,
                    Scalar move_ratio,
                    unsigned int seed,
                    unsigned int nselect,
                    unsigned int nsweeps,
                    bool pretend,
                    bool multiphase,
                    unsigned int numphase);

    ~UpdaterShape();

    std::vector< std::string > getProvidedLogQuantities();

    //! Calculates the requested log value and returns it
    Scalar getLogValue(const std::string& quantity, unsigned int timestep);

    void update(unsigned int timestep);

    void initialize();

    unsigned int getAcceptedCount(unsigned int ndx) { return m_count_accepted[ndx]; }

    unsigned int getTotalCount(unsigned int ndx) { return m_count_total[ndx]; }

    unsigned int getAcceptedB1(unsigned int ndx) { return m_b1_accepted[ndx]; }

    unsigned int getAcceptedB2(unsigned int ndx) { return m_b2_accepted[ndx]; }

    unsigned int getAcceptedB3(unsigned int ndx) { return m_b3_accepted[ndx]; }

    unsigned int getTotalB1(unsigned int ndx) { return m_b1_total[ndx]; }

    unsigned int getTotalB2(unsigned int ndx) { return m_b2_total[ndx]; }

    unsigned int getTotalB3(unsigned int ndx) { return m_b3_total[ndx]; }

    void resetStatistics()
        {
        std::fill(m_count_accepted.begin(), m_count_accepted.end(), 0);
        std::fill(m_count_total.begin(), m_count_total.end(), 0);
        std::fill(m_b1_accepted.begin(), m_b1_accepted.end(), 0);
        std::fill(m_b2_accepted.begin(), m_b2_accepted.end(), 0);
        std::fill(m_b3_accepted.begin(), m_b3_accepted.end(), 0);
        std::fill(m_b1_total.begin(), m_b1_total.end(), 0);
        std::fill(m_b2_total.begin(), m_b2_total.end(), 0);
        std::fill(m_b3_total.begin(), m_b3_total.end(), 0);
        }

    void registerLogBoltzmannFunction(std::shared_ptr< ShapeLogBoltzmannFunction<Shape> >  lbf);

    void registerShapeMove(std::shared_ptr<shape_move_function<Shape, Saru> > move);

    Scalar getStepSize(unsigned int typ)
        {
        if(m_move_function) return m_move_function->getStepSize(typ);
        return 0.0;
        }

    void setStepSize(unsigned int typ, Scalar stepsize)
        {
        if(m_move_function) m_move_function->setStepSize(typ, stepsize);
        }


    void countTypes();

    //! Method that is called whenever the GSD file is written if connected to a GSD file.
    int slotWriteGSD(gsd_handle&, std::string name) const;

    //! Method that is called to connect to the gsd write state signal
    void connectGSDSignal(std::shared_ptr<GSDDumpWriter> writer, std::string name);

    //! Method that is called to connect to the gsd write state signal
    bool restoreStateGSD(std::shared_ptr<GSDReader> reader, std::string name);

private:
    unsigned int                m_seed;           //!< Random number seed
    int                m_global_partition;           //!< Random number seed
    unsigned int                m_nselect;
    unsigned int                m_nsweeps;
    std::vector<unsigned int>   m_count_accepted;
    std::vector<unsigned int>   m_count_total;
    std::vector<unsigned int>   m_b1_accepted;
    std::vector<unsigned int>   m_b2_accepted;
    std::vector<unsigned int>   m_b3_accepted;
    std::vector<unsigned int>   m_b1_total;
    std::vector<unsigned int>   m_b2_total;
    std::vector<unsigned int>   m_b3_total;
    unsigned int                m_move_ratio;

    std::shared_ptr< shape_move_function<Shape, Saru> >   m_move_function;
    std::shared_ptr< IntegratorHPMCMono<Shape> >          m_mc;
    std::shared_ptr< ShapeLogBoltzmannFunction<Shape> >   m_log_boltz_function;

    GPUArray< Scalar >          m_determinant;
    GPUArray< unsigned int >    m_ntypes;

    std::vector< std::string >  m_ProvidedQuantities;
    size_t                      m_num_params;
    bool                        m_pretend;
    bool                        m_initialized;
    bool                        m_multi_phase;
    unsigned int                m_num_phase;
    detail::UpdateOrder         m_update_order;         //!< Update order

};

template < class Shape >
UpdaterShape<Shape>::UpdaterShape(std::shared_ptr<SystemDefinition> sysdef,
                                 std::shared_ptr< IntegratorHPMCMono<Shape> > mc,
                                 Scalar move_ratio,
                                 unsigned int seed,
                                 unsigned int nselect,
                                 unsigned int nsweeps,
                                 bool pretend,
                                 bool multiphase,
                                 unsigned int numphase)
    : Updater(sysdef), m_seed(seed), m_global_partition(0), m_nselect(nselect),m_nsweeps(nsweeps),
      m_move_ratio(move_ratio*65535), m_mc(mc),
      m_determinant(m_pdata->getNTypes(), m_exec_conf),
      m_ntypes(m_pdata->getNTypes(), m_exec_conf), m_num_params(0),
      m_pretend(pretend), m_initialized(false), m_multi_phase(multiphase), m_num_phase(numphase), m_update_order(seed)
    {
    m_count_accepted.resize(m_pdata->getNTypes(), 0);
    m_count_total.resize(m_pdata->getNTypes(), 0);
    m_b1_accepted.resize(m_pdata->getNTypes(), 0);
    m_b2_accepted.resize(m_pdata->getNTypes(), 0);
    m_b3_accepted.resize(m_pdata->getNTypes(), 0);
    m_b1_total.resize(m_pdata->getNTypes(), 0);
    m_b2_total.resize(m_pdata->getNTypes(), 0);
    m_b3_total.resize(m_pdata->getNTypes(), 0);
    m_nselect = (m_pdata->getNTypes() < m_nselect) ? m_pdata->getNTypes() : m_nselect;
    m_ProvidedQuantities.push_back("shape_move_acceptance_ratio");
    m_ProvidedQuantities.push_back("shape_move_particle_volume");
    m_ProvidedQuantities.push_back("shape_move_two_phase_box1");
    m_ProvidedQuantities.push_back("shape_move_two_phase_box2");
    m_ProvidedQuantities.push_back("shape_move_two_phase_box3");
    ArrayHandle<Scalar> h_det(m_determinant, access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);
    for(size_t i = 0; i < m_pdata->getNTypes(); i++)
    m_ProvidedQuantities.push_back("shape_move_energy");
        {
        ArrayHandle<Scalar> h_det(m_determinant, access_location::host, access_mode::readwrite);
        ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);
        for(size_t i = 0; i < m_pdata->getNTypes(); i++)
            {
            h_det.data[i] = 0.0;
            h_ntypes.data[i] = 0;
            }
        }
    countTypes(); // TODO: connect to ntypes change/particle changes to resize arrays and count them up again.
    //TODO: add a sanity check to makesure that MPI is setup correctly
    if(m_multi_phase)
    {
    #ifdef ENABLE_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &m_global_partition);
    assert(m_global_partition < 2);
    #endif
    }

    }

template< class Shape >
UpdaterShape<Shape>::~UpdaterShape() { m_exec_conf->msg->notice(5) << "Destroying UpdaterShape "<< std::endl; }

/*! hpmc::UpdaterShape provides:
\returns a list of provided quantities
*/
template < class Shape >
std::vector< std::string > UpdaterShape<Shape>::getProvidedLogQuantities()
    {
    return m_ProvidedQuantities;
    }
//! Calculates the requested log value and returns it
template < class Shape >
Scalar UpdaterShape<Shape>::getLogValue(const std::string& quantity, unsigned int timestep)
    {
    Scalar value = 0.0;
    if(m_move_function->getLogValue(quantity, timestep, value) || m_log_boltz_function->getLogValue(quantity, timestep, value))
        {
        return value;
        }
    else if(quantity == "shape_move_acceptance_ratio")
        {
        unsigned int ctAccepted = 0, ctTotal = 0;
        ctAccepted = std::accumulate(m_count_accepted.begin(), m_count_accepted.end(), 0);
        ctTotal = std::accumulate(m_count_total.begin(), m_count_total.end(), 0);
        return ctTotal ? Scalar(ctAccepted)/Scalar(ctTotal) : 0;
        }
    else if(quantity == "shape_move_particle_volume")
        {
        ArrayHandle< unsigned int > h_ntypes(m_ntypes, access_location::host, access_mode::read);
        // ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::readwrite);
        auto params = m_mc->getParams();
        double volume = 0.0;
        for(size_t i = 0; i < m_pdata->getNTypes(); i++)
            {
            detail::mass_properties<Shape> mp(params[i]);
            volume += mp.getVolume()*Scalar(h_ntypes.data[i]);
            }
		return volume;
		}
    else if(quantity == "shape_move_two_phase_box1")
        {
        unsigned int b1Accepted = 0, b1Total = 0;
        b1Accepted = std::accumulate(m_b1_accepted.begin(), m_b1_accepted.end(), 0);
        b1Total = std::accumulate(m_b1_total.begin(), m_b1_total.end(), 0);
        //std::cout << "it got here";
        return b1Total ? Scalar(b1Accepted)/Scalar(b1Total) : 0;
        }
    else if(quantity == "shape_move_two_phase_box2")
        {
        unsigned int b2Accepted = 0, b2Total = 0;
        b2Accepted = std::accumulate(m_b2_accepted.begin(), m_b2_accepted.end(), 0);
        b2Total = std::accumulate(m_b2_total.begin(), m_b2_total.end(), 0);
        return b2Total ? Scalar(b2Accepted)/Scalar(b2Total) : 0;
        }
	else if(quantity == "shape_move_two_phase_box3")
        {
        unsigned int b3Accepted = 0, b3Total = 0;
        b3Accepted = std::accumulate(m_b3_accepted.begin(), m_b3_accepted.end(), 0);
        b3Total = std::accumulate(m_b3_total.begin(), m_b3_total.end(), 0);
        return b3Total ? Scalar(b3Accepted)/Scalar(b3Total) : 0;
        }
	    else
		{
		for(size_t i = 0; i < m_num_params; i++)
		    {
		    if(quantity == getParamName(i))
			{
			return m_move_function->getParam(i);
			}
		    }

        return volume;
        }
    else if(quantity == "shape_move_energy")
        {
        Scalar energy = 0.0;
        ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);
        ArrayHandle<Scalar> h_det(m_determinant, access_location::host, access_mode::readwrite);
        // ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::readwrite);
        for(unsigned int i = 0; i < m_pdata->getNTypes(); i++)
            {
            energy += m_log_boltz_function->computeEnergy(timestep, h_ntypes.data[i], i, m_mc->getParams()[i], h_det.data[i]);
            }
        return energy;
        }
    else
	    {
>>>>>>> origin/alchem
		m_exec_conf->msg->error() << "update.shape: " << quantity << " is not a valid log quantity" << std::endl;
		throw std::runtime_error("Error getting log value");
		}
    }

/*! Perform Metropolis Monte Carlo shape deformations
\param timestep Current time step of the simulation
*/
template < class Shape >
void UpdaterShape<Shape>::update(unsigned int timestep)
    {
    typedef std::vector<typename Shape::param_type, managed_allocator<typename Shape::param_type> > param_vector;
    m_exec_conf->msg->notice(4) << "UpdaterShape update: " << timestep << ", initialized: "<< std::boolalpha << m_initialized << " @ " << std::hex << this << std::dec << std::endl;
    bool warn = !m_initialized;
    if(!m_initialized)
        initialize();
    if(!m_move_function || !m_log_boltz_function)
        {
    	if(warn) m_exec_conf->msg->warning() << "update.shape: runing without a move function! " << std::endl;
    	return;
        }

    Saru rng(m_move_ratio, m_seed, timestep);
    unsigned int move_type_select = rng.u32() & 0xffff;
    bool move = (move_type_select < m_move_ratio);
    if (!move)
        return;
    if (this->m_prof)
        this->m_prof->push(this->m_exec_conf, "UpdaterShape update");

    m_update_order.resize(m_pdata->getNTypes());
    for(unsigned int sweep=0; sweep < m_nsweeps; sweep++)
        {
        // make a trial move for i
        int typ_i = m_update_order[cur_type];
        m_exec_conf->msg->notice(5) << " UpdaterShape making trial move for typeid=" << typ_i << ", " << cur_type << std::endl;
        m_count_total[typ_i]++;
        m_b1_total[typ_i]++;
        m_b2_total[typ_i]++;
        m_b3_total[typ_i]++;
        // access parameters
        typename Shape::param_type param;
            { // need to scope because we set at the end of loop
            ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::readwrite);
            param = h_params.data[typ_i];
        if (this->m_prof)
            this->m_prof->push(this->m_exec_conf, "UpdaterShape setup");
        // Shuffle the order of particles for this sweep
        m_update_order.choose(timestep+40591, m_nselect, sweep+91193); // order of the list doesn't matter the probability of each combination is the same.

        Scalar log_boltz = 0.0;
        m_exec_conf->msg->notice(6) << "UpdaterShape copying data" << std::endl;
        if (this->m_prof)
            this->m_prof->push(this->m_exec_conf, "UpdaterShape copy param");

        param_vector& params = m_mc->getParams();
        param_vector param_copy(m_nselect);
        for (unsigned int i = 0; i < m_nselect; i++)
            {
            param_copy[i] = params[m_update_order[i]];
            }
        
        Saru rng_i(typ_i, m_seed + m_nselect + typ_i, timestep);
        m_move_function->construct(timestep, typ_i, param, rng_i);
        h_det.data[typ_i] = m_move_function->getDeterminant(); // new determinant
        m_exec_conf->msg->notice(5) << " UpdaterShape I=" << h_det.data[typ_i] << ", " << h_det_backup.data[typ_i] << std::endl;
        // energy and moment of interia change.
        log_boltz += (*m_log_boltz_function)(
                                                h_ntypes.data[typ_i],           // number of particles of type typ_i,
                                                typ_i,                          // the type id
                                                param,                          // new shape parameter
                                                h_det.data[typ_i],              // new determinant
                                                h_param_backup.data[typ_i],     // old shape parameter
                                                h_det_backup.data[typ_i]        // old determinant
                                            );
        m_mc->setParam(typ_i, param);
        }
    // calculate boltzmann factor.
    bool accept = false, reject=true; // looks redundant but it is not because of the pretend mode.
    Scalar p = rng.s(Scalar(0.0),Scalar(1.0)), Z = fast::exp(log_boltz);
    m_exec_conf->msg->notice(8) << " UpdaterShape p=" << p << ", z=" << Z << std::endl;
    if(m_multi_phase)
        {
        //m_exec_conf->msg->notice(8) << " Random (b) seed is " << p << std::endl;
        #ifdef ENABLE_MPI
        // make sure random seeds are equal
        //m_exec_conf->msg->notice(8) << " Random seed is " << p << std::endl;
        if(m_num_phase == 2)
            {
            Scalar Z_0 = Z;
            Scalar Z_1 = Z;
        //Scalar Z_0 = 1.0, Z_1 = 1.0;
/*        if(m_global_partition == 0)
            {
            Z_0 = Z;
            m_exec_conf->msg->notice(8) << timestep <<" Z0 (b) is " << Z_0 << std::endl;

            //MPI_Send( &Z_loc, 1, MPI_HOOMD_SCALAR, 1, 0, MPI_COMM_WORLD);
            //MPI_Status stat;
            //MPI_Recv( &Z_other, 1, MPI_HOOMD_SCALAR, 1, 0, MPI_COMM_WORLD, &stat);
            }
        else if (m_global_partition == 1)
            {
            Z_1 = Z;
            m_exec_conf->msg->notice(8) << timestep <<" Z1 (b) is " << Z_1 << std::endl;

            //MPI_Send( &Z_loc, 1, MPI_HOOMD_SCALAR, 0, 0, MPI_COMM_WORLD);
            //MPI_Status stat;
            //MPI_Recv( &Z_other, 1, MPI_HOOMD_SCALAR, 0, 0, MPI_COMM_WORLD, &stat);
            }
*/
            MPI_Bcast( &Z_0, 1, MPI_HOOMD_SCALAR, 0, MPI_COMM_WORLD );
            MPI_Bcast( &Z_1, 1, MPI_HOOMD_SCALAR, 1, MPI_COMM_WORLD );
            Z = Z_0*Z_1;
            m_exec_conf->msg->notice(8) << " UpdaterShape Z0=" << Z_0 << ", Z1 = "<< Z_1 << ", z=" << Z << std::endl;
            }
        if(m_num_phase == 3)
            {
            Scalar Z_0 = Z;
            Scalar Z_1 = Z;
            Scalar Z_2 = Z;
            MPI_Bcast( &Z_0, 1, MPI_HOOMD_SCALAR, 0, MPI_COMM_WORLD );
            MPI_Bcast( &Z_1, 1, MPI_HOOMD_SCALAR, 1, MPI_COMM_WORLD );
            MPI_Bcast( &Z_2, 1, MPI_HOOMD_SCALAR, 2, MPI_COMM_WORLD );
            Z = Z_0*Z_1*Z_2;
            m_exec_conf->msg->notice(8) << " UpdaterShape Z0=" << Z_0 << ", Z1 = "<< Z_1 << ", Z2 = "<< Z_2 << ", z=" << Z << std::endl;
            }
        #endif
        }

    if( p < Z)
        {
        unsigned int overlaps = m_mc->countOverlaps(timestep, true);
        accept = !overlaps;
        m_exec_conf->msg->notice(5) << " UpdaterShape counted " << overlaps << " overlaps" << std::endl;
        if(m_multi_phase)
            {
            #ifdef ENABLE_MPI
            // make sure random seeds are equal
            if(m_num_phase == 2)
                {
                bool a_0 = accept, a_1 = accept;
            //Scalar a_other;
/*            if(m_global_partition == 0)
                {
                a_0 = accept;
                m_exec_conf->msg->notice(8) << timestep <<" a_0 (b) is " << a_0 << std::endl;
                
            //    MPI_Send( &a_0, 1, MPI_C_BOOL, 1, 0, MPI_COMM_WORLD);
            //    MPI_Status stat;
            //    MPI_Recv( &a_1, 1, MPI_C_BOOL, 1, 0, MPI_COMM_WORLD, &stat);
                }
            else if (m_global_partition == 1)
                {
                a_1 = accept;
                m_exec_conf->msg->notice(8) << timestep << " a_1 (b) is " << a_1 << std::endl;
                
            //   
            //    MPI_Status stat;
            //    MPI_Recv( &a_0, 1, MPI_C_BOOL, 0, 0, MPI_COMM_WORLD, &stat);
            //    MPI_Send( &a_1, 1, MPI_C_BOOL, 0, 0, MPI_COMM_WORLD);
                }*/
                MPI_Bcast( &a_0, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD );
                MPI_Bcast( &a_1, 1, MPI_C_BOOL, 1, MPI_COMM_WORLD );
            //m_exec_conf->msg->notice(8) << " a_0 is " << a_0 << std::endl;
            //m_exec_conf->msg->notice(8) << " a_1 is " << a_1 << std::endl;
                accept = a_0 && a_1;
                m_exec_conf->msg->notice(8) << timestep <<" UpdaterShape a0=" << a_0 << ", a1 = "<< a_1 << ", a=" << accept << std::endl;
                if ( a_0 )
                    {
                    for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                        {
                        int typ_i = m_update_order[cur_type];
                        m_b1_accepted[typ_i]++;
                        }
                    }
                if ( a_1 )
                    {
                    for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                        {
                        int typ_i = m_update_order[cur_type];
                        m_b2_accepted[typ_i]++;
                        }
                    }
                }
            if(m_num_phase == 3)
                {
                bool a_0 = accept, a_1 = accept, a_2 = accept;
                MPI_Bcast( &a_0, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD );
                MPI_Bcast( &a_1, 1, MPI_C_BOOL, 1, MPI_COMM_WORLD );
                MPI_Bcast( &a_2, 1, MPI_C_BOOL, 2, MPI_COMM_WORLD );
                accept = a_0 && a_1 && a_2;
                m_exec_conf->msg->notice(8) << timestep <<" UpdaterShape a0=" << a_0 << ", a1 = "<< a_1 << ", a2 = " << a_2 << ", a=" << accept << std::endl;
                
                if ( a_0 )
                    {
                    for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                        {
                        int typ_i = m_update_order[cur_type];
                        m_b1_accepted[typ_i]++;
                        }
                    }
                if ( a_1 )
                    {
                    for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                        {
                        int typ_i = m_update_order[cur_type];
                        m_b2_accepted[typ_i]++;
                        }
                    }
                if ( a_2 )
                    {
                    for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                        {
                        int typ_i = m_update_order[cur_type];
                        m_b3_accepted[typ_i]++;
                        }
                    }
                }
             
            #endif
            }
        m_exec_conf->msg->notice(5) << " UpdaterShape p=" << p << ", z=" << Z << std::endl;
        }
=======
        if (this->m_prof)
            this->m_prof->pop();

        if (this->m_prof)
            this->m_prof->push(this->m_exec_conf, "UpdaterShape move");
        GPUArray< Scalar > determinant_backup(m_determinant);
        m_move_function->prepare(timestep);
>>>>>>> origin/alchem

        for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
            {
            // make a trial move for i
            int typ_i = m_update_order[cur_type];
            m_exec_conf->msg->notice(5) << " UpdaterShape making trial move for typeid=" << typ_i << ", " << cur_type << std::endl;
            m_count_total[typ_i]++;
            // access parameters
            typename Shape::param_type param;
                // { // need to scope because we set at the end of loop
                // ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::readwrite);
            param = params[typ_i];
                // }
            // ArrayHandle<typename Shape::param_type> h_param_backup(param_copy, access_location::host, access_mode::readwrite);
            ArrayHandle<Scalar> h_det(m_determinant, access_location::host, access_mode::readwrite);
            ArrayHandle<Scalar> h_det_backup(determinant_backup, access_location::host, access_mode::readwrite);
            ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);

            Saru rng_i(m_seed + m_nselect + sweep + m_nsweeps, typ_i+1046527, timestep+7919);
            m_move_function->construct(timestep, typ_i, param, rng_i);
            h_det.data[typ_i] = m_move_function->getDeterminant(); // new determinant
            m_exec_conf->msg->notice(5) << " UpdaterShape I=" << h_det.data[typ_i] << ", " << h_det_backup.data[typ_i] << std::endl;
            // energy and moment of interia change.
            assert(h_det.data[typ_i] != 0 && h_det_backup.data[typ_i] != 0);
            log_boltz += (*m_log_boltz_function)(   timestep,
                                                    h_ntypes.data[typ_i],           // number of particles of type typ_i,
                                                    typ_i,                          // the type id
                                                    param,                          // new shape parameter
                                                    h_det.data[typ_i],              // new determinant
                                                    param_copy[cur_type],           // old shape parameter
                                                    h_det_backup.data[typ_i]        // old determinant
                                                );
            m_mc->setParam(typ_i, param, cur_type == (m_nselect-1));
            }
        if (this->m_prof)
            this->m_prof->pop();

        if (this->m_prof)
            this->m_prof->push(this->m_exec_conf, "UpdaterShape cleanup");
        // calculate boltzmann factor.
        bool accept = false, reject=true; // looks redundant but it is not because of the pretend mode.
        Scalar p = rng.s(Scalar(0.0),Scalar(1.0)), Z = fast::exp(log_boltz);
        m_exec_conf->msg->notice(5) << " UpdaterShape p=" << p << ", z=" << Z << std::endl;
        if( p < Z)
            {
            unsigned int overlaps = 1;
            if(m_pdata->getNTypes() == m_pdata->getNGlobal())
                {
                overlaps = m_mc->countOverlapsEx(timestep, true, m_update_order.begin(), m_update_order.begin()+m_nselect);
                // unsigned int overlaps_check = m_mc->countOverlaps(timestep, false);
                // if(overlaps != overlaps_check)
                //     {
                //     std::cout << "The particles are: ";
                //     for(size_t i = 0; i < m_nselect; i++)
                //         {
                //             std::cout << m_update_order[i] << ", ";
                //         }
                //     std::cout << std::endl;
                //
                //     std::cout << "ERROR !!!!!! overlaps detected!" << "overlap is "<< overlaps << " check is "<< overlaps_check << std::endl;
                //     throw std::runtime_error("ERROR !!!!!! overlaps detected!");
                //     }
                }
            else
                {
                overlaps = m_mc->countOverlaps(timestep, true);
                }
            accept = !overlaps;
            m_exec_conf->msg->notice(5) << " UpdaterShape counted " << overlaps << " overlaps" << std::endl;
            }

        if( !accept ) // catagorically reject the move.
            {
            m_exec_conf->msg->notice(5) << " UpdaterShape move retreating" << std::endl;
            m_move_function->retreat(timestep);
            }
        else if( m_pretend ) // pretend to accept the move but actually reject it.
            {
            m_exec_conf->msg->notice(5) << " UpdaterShape move accepted -- pretend mode" << std::endl;
            m_move_function->retreat(timestep);
            for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                {
                int typ_i = m_update_order[cur_type];
                m_count_accepted[typ_i]++;
                }
            }
        else    // actually accept the move.
            {
            m_exec_conf->msg->notice(5) << " UpdaterShape move accepted" << std::endl;
            for (unsigned int cur_type = 0; cur_type < m_nselect; cur_type++)
                {
                int typ_i = m_update_order[cur_type];
                m_count_accepted[typ_i]++;
                }
            // m_move_function->advance(timestep);
            reject = false;
            }

        if(reject)
            {
            m_exec_conf->msg->notice(5) << " UpdaterShape move rejected" << std::endl;
            m_determinant.swap(determinant_backup);
            // m_mc->swapParams(param_copy);
            // ArrayHandle<typename Shape::param_type> h_param_copy(param_copy, access_location::host, access_mode::readwrite);
            for(size_t typ = 0; typ < m_nselect; typ++)
                {
                m_mc->setParam(m_update_order[typ], param_copy[typ], typ == (m_nselect-1)); // set the params.
                }
            }
        if (this->m_prof)
            this->m_prof->pop();
        }
    if (this->m_prof)
        this->m_prof->pop();
    m_exec_conf->msg->notice(4) << " UpdaterShape update done" << std::endl;
    }

template< typename Shape>
void UpdaterShape<Shape>::initialize()
    {
    ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar> h_det(m_determinant, access_location::host, access_mode::readwrite);
    // ArrayHandle<typename Shape::param_type> h_params(m_mc->getParams(), access_location::host, access_mode::readwrite);
    auto params = m_mc->getParams();
    for(size_t i = 0; i < m_pdata->getNTypes(); i++)
        {
        detail::mass_properties<Shape> mp(params[i]);
        h_det.data[i] = mp.getDeterminant();
        }
    m_initialized = true;
    }

template< typename Shape>
void UpdaterShape<Shape>::registerLogBoltzmannFunction(std::shared_ptr< ShapeLogBoltzmannFunction<Shape> >  lbf)
    {
    if(m_log_boltz_function)
        return;
    m_log_boltz_function = lbf;
    std::vector< std::string > quantities(m_log_boltz_function->getProvidedLogQuantities());
    m_ProvidedQuantities.reserve( m_ProvidedQuantities.size() + quantities.size() );
    m_ProvidedQuantities.insert(m_ProvidedQuantities.end(), quantities.begin(), quantities.end());
    }

template< typename Shape>
void UpdaterShape<Shape>::registerShapeMove(std::shared_ptr<shape_move_function<Shape, Saru> > move)
    {
    if(m_move_function) // if it exists I do not want to reset it.
        return;
    m_move_function = move;
    std::vector< std::string > quantities(m_move_function->getProvidedLogQuantities());
    m_ProvidedQuantities.reserve( m_ProvidedQuantities.size() + quantities.size() );
    m_ProvidedQuantities.insert(m_ProvidedQuantities.end(), quantities.begin(), quantities.end());
    }

template< typename Shape>
void UpdaterShape<Shape>::countTypes()
    {
    // zero the array.
    ArrayHandle<unsigned int> h_ntypes(m_ntypes, access_location::host, access_mode::readwrite);
    for(size_t i = 0; i < m_pdata->getNTypes(); i++)
        {
        h_ntypes.data[i] = 0;
        }

    ArrayHandle<Scalar4> h_postype(m_pdata->getPositions(), access_location::host, access_mode::read);
    for(size_t j= 0; j < m_pdata->getN(); j++)
        {
        int typ_j = __scalar_as_int(h_postype.data[j].w);
        h_ntypes.data[typ_j]++;
        }

    #ifdef ENABLE_MPI
    if (this->m_pdata->getDomainDecomposition())
        {
        MPI_Allreduce(MPI_IN_PLACE, h_ntypes.data, m_pdata->getNTypes(), MPI_UNSIGNED, MPI_SUM, m_exec_conf->getMPICommunicator());
        }
    #endif
    }

template< typename Shape>
int UpdaterShape<Shape>::slotWriteGSD(gsd_handle& handle, std::string name) const
    {
    m_exec_conf->msg->notice(2) << "UpdaterShape writing to GSD File to name: "<< name << std::endl;
    int retval = 0;
    // create schema helpers
    #ifdef ENABLE_MPI
    bool mpi=(bool)m_pdata->getDomainDecomposition();
    #else
    bool mpi=false;
    #endif

    retval |= m_move_function->writeGSD(handle, name+"move/", m_exec_conf, mpi);

    return retval;
    }

template< typename Shape>
void UpdaterShape<Shape>::connectGSDSignal(std::shared_ptr<GSDDumpWriter> writer, std::string name)
    {
    _connectGSDSignal(this, writer, name); // call through to the helper function.
    }

template< typename Shape>
bool UpdaterShape<Shape>::restoreStateGSD(std::shared_ptr<GSDReader> reader, std::string name)
    {
    bool success = true;
    m_exec_conf->msg->notice(2) << "UpdaterShape from GSD File to name: "<< name << std::endl;
    uint64_t frame = reader->getFrame();
    // create schemas
    #ifdef ENABLE_MPI
    bool mpi=(bool)m_pdata->getDomainDecomposition();
    #else
    bool mpi=false;
    #endif
    success = m_move_function->restoreStateGSD(reader, frame, name+"move/", m_pdata->getNTypes(), m_exec_conf, mpi) && success;
    return success;
    }

template< typename Shape >
void export_UpdaterShape(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< UpdaterShape<Shape>, std::shared_ptr< UpdaterShape<Shape> > >(m, name.c_str(), pybind11::base<Updater>())
    .def( pybind11::init<   std::shared_ptr<SystemDefinition>,
                            std::shared_ptr< IntegratorHPMCMono<Shape> >,
                            Scalar,
                            unsigned int,
                            unsigned int,
                            unsigned int,
                            bool,
                            bool,
                            unsigned int>())
    .def("getAcceptedCount", &UpdaterShape<Shape>::getAcceptedCount)
    .def("getTotalCount", &UpdaterShape<Shape>::getTotalCount)
    .def("registerShapeMove", &UpdaterShape<Shape>::registerShapeMove)
    .def("registerLogBoltzmannFunction", &UpdaterShape<Shape>::registerLogBoltzmannFunction)
    .def("resetStatistics", &UpdaterShape<Shape>::resetStatistics)
    .def("getStepSize", &UpdaterShape<Shape>::getStepSize)
    .def("setStepSize", &UpdaterShape<Shape>::setStepSize)
    .def("connectGSDSignal", &UpdaterShape<Shape>::connectGSDSignal)
    .def("restoreStateGSD", &UpdaterShape<Shape>::restoreStateGSD)
    ;
    }


} // namespace



#endif