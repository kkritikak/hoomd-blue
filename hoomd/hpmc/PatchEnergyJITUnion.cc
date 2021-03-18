#include "PatchEnergyJITUnion.h"

#include "hoomd/hpmc/OBBTree.h"

#ifdef ENABLE_TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>
#endif

//Builds OBB tree based on geometric properties of the constituent particles
void PatchEnergyJITUnion::buildOBBTree()
    {

    if (m_build_obb)
        {
        for (unsigned int ti = 0; ti < m_updated_types.size(); ti++)
            {

            unsigned int type = m_updated_types[ti];
            unsigned int N = (unsigned int) m_position[type].size();
            hpmc::detail::OBB *obbs = new hpmc::detail::OBB[N];
            // extract member parameters, positions, and orientations and compute the rcut along the way
            float extent_i = 0.0;
            for (unsigned int i = 0; i < N; i++)
                {
                // use a spherical OBB of radius 0.5*d
                auto pos = m_position[type][i];
                float diameter = m_diameter[type][i];

                // use a spherical OBB of radius 0.5*d
                obbs[i] = hpmc::detail::OBB(pos,0.5f*diameter);

                Scalar r = sqrt(dot(pos,pos))+0.5f*diameter;
                extent_i = std::max(extent_i, float(2*r));

               // we do not support exclusions
                obbs[i].mask = 1;
                }

            // set the diameter
            m_extent_type[type] = extent_i;

            // build tree and store proxy structure
            hpmc::detail::OBBTree tree;
            tree.buildTree(obbs, N, m_leaf_capacity, false);
            delete [] obbs;
            m_tree[type] = hpmc::detail::GPUTree(tree, m_managed_memory);
            }

        m_updated_types.clear();
        m_build_obb = false;
        }
    }

float PatchEnergyJITUnion::compute_leaf_leaf_energy(vec3<float> dr,
                             unsigned int type_a,
                             unsigned int type_b,
                             const quat<float>& orientation_a,
                             const quat<float>& orientation_b,
                             unsigned int cur_node_a,
                             unsigned int cur_node_b)
    {
    float energy = 0.0;
    vec3<float> r_ab = rotate(conj(quat<float>(orientation_b)),vec3<float>(dr));

    // loop through leaf particles of cur_node_a
    unsigned int na = m_tree[type_a].getNumParticles(cur_node_a);
    unsigned int nb = m_tree[type_b].getNumParticles(cur_node_b);

    for (unsigned int i= 0; i < na; i++)
        {
        unsigned int ileaf = m_tree[type_a].getParticleByNode(cur_node_a, i);

        unsigned int type_i = m_type[type_a][ileaf];
        quat<float> orientation_i = conj(quat<float>(orientation_b))*quat<float>(orientation_a) * m_orientation[type_a][ileaf];
        vec3<float> pos_i(rotate(conj(quat<float>(orientation_b))*quat<float>(orientation_a),m_position[type_a][ileaf])-r_ab);

        // loop through leaf particles of cur_node_b
        for (unsigned int j= 0; j < nb; j++)
            {
            unsigned int jleaf = m_tree[type_b].getParticleByNode(cur_node_b, j);

            unsigned int type_j = m_type[type_b][jleaf];
            quat<float> orientation_j = m_orientation[type_b][jleaf];
            vec3<float> r_ij = m_position[type_b][jleaf] - pos_i;

            float rsq = dot(r_ij,r_ij);
            float rcut = float(m_rcut_union+0.5*(m_diameter[type_a][ileaf]+m_diameter[type_b][jleaf]));
            if (rsq <= rcut*rcut)
                {
                // evaluate energy via JIT function
                energy += m_eval_union(r_ij,
                    type_i,
                    orientation_i,
                    m_diameter[type_a][ileaf],
                    m_charge[type_a][ileaf],
                    type_j,
                    orientation_j,
                    m_diameter[type_b][jleaf],
                    m_charge[type_b][jleaf]);
                }
            }
        }
    return energy;
    }


float PatchEnergyJITUnion::energy(const vec3<float>& r_ij,
                                  unsigned int type_i,
                                  const quat<float>& q_i,
                                  float d_i,
                                  float charge_i,
                                  unsigned int type_j,
                                  const quat<float>& q_j,
                                  float d_j,
                                  float charge_j)
    {

    const hpmc::detail::GPUTree& tree_a = m_tree[type_i];
    const hpmc::detail::GPUTree& tree_b = m_tree[type_j];

    float energy = 0.0;

    // evaluate isotropic part if necessary
    if (m_r_cut >= 0.0)
        energy += m_eval(r_ij, type_i, q_i, d_i, charge_i, type_j, q_j, d_j, charge_j);

    if (tree_a.getNumLeaves() <= tree_b.getNumLeaves())
        {
        #ifdef ENABLE_TBB
        energy += tbb::parallel_reduce(tbb::blocked_range<unsigned int>(0, tree_a.getNumLeaves()),
            0.0f,
            [&](const tbb::blocked_range<unsigned int>& r, float energy)->float {
            for (unsigned int cur_leaf_a = r.begin(); cur_leaf_a != r.end(); ++cur_leaf_a)
        #else
        for (unsigned int cur_leaf_a = 0; cur_leaf_a < tree_a.getNumLeaves(); cur_leaf_a ++)
        #endif
            {
            unsigned int cur_node_a = tree_a.getLeafNode(cur_leaf_a);
            hpmc::detail::OBB obb_a = tree_a.getOBB(cur_node_a);

            // add range of interaction
            obb_a.lengths.x += float(m_rcut_union);
            obb_a.lengths.y += float(m_rcut_union);
            obb_a.lengths.z += float(m_rcut_union);

            // rotate and translate a's obb into b's body frame
            obb_a.affineTransform(conj(q_j)*q_i, rotate(conj(q_j),-r_ij));

            unsigned cur_node_b = 0;
            while (cur_node_b < tree_b.getNumNodes())
                {
                unsigned int query_node = cur_node_b;
                if (tree_b.queryNode(obb_a, cur_node_b))
                    energy += compute_leaf_leaf_energy(r_ij, type_i, type_j, q_i, q_j, cur_node_a, query_node);
                }
            }
        #ifdef ENABLE_TBB
        return energy;
        }, [](float x, float y)->float { return x+y; } );
        #endif
        }
    else
        {
        #ifdef ENABLE_TBB
        energy += tbb::parallel_reduce(tbb::blocked_range<unsigned int>(0, tree_b.getNumLeaves()),
            0.0f,
            [&](const tbb::blocked_range<unsigned int>& r, float energy)->float {
            for (unsigned int cur_leaf_b = r.begin(); cur_leaf_b != r.end(); ++cur_leaf_b)
        #else
        for (unsigned int cur_leaf_b = 0; cur_leaf_b < tree_b.getNumLeaves(); cur_leaf_b ++)
        #endif
            {
            unsigned int cur_node_b = tree_b.getLeafNode(cur_leaf_b);
            hpmc::detail::OBB obb_b = tree_b.getOBB(cur_node_b);

            // add range of interaction
            obb_b.lengths.x += float(m_rcut_union);
            obb_b.lengths.y += float(m_rcut_union);
            obb_b.lengths.z += float(m_rcut_union);

            // rotate and translate b's obb into a's body frame
            obb_b.affineTransform(conj(q_i)*q_j, rotate(conj(q_i),r_ij));

            unsigned cur_node_a = 0;
            while (cur_node_a < tree_a.getNumNodes())
                {
                unsigned int query_node = cur_node_a;
                if (tree_a.queryNode(obb_b, cur_node_a))
                    energy += compute_leaf_leaf_energy(-r_ij, type_j, type_i, q_j, q_i, cur_node_b, query_node);
                }
            }
        #ifdef ENABLE_TBB
        return energy;
        }, [](float x, float y)->float { return x+y; } );
        #endif
        }

    return energy;
    }

void export_PatchEnergyJITUnion(pybind11::module &m)
    {
    pybind11::class_<PatchEnergyJITUnion, PatchEnergyJIT, std::shared_ptr<PatchEnergyJITUnion> >(m, "PatchEnergyJITUnion")
            .def(pybind11::init< std::shared_ptr<SystemDefinition>,
                                 std::shared_ptr<ExecutionConfiguration>,
                                 const std::string&, Scalar, const unsigned int,
                                 const std::string&, Scalar, const unsigned int >())

            .def("getPositions", &PatchEnergyJITUnion::getPositions)
            .def("setPositions", &PatchEnergyJITUnion::setPositions)
            .def("getOrientations", &PatchEnergyJITUnion::getOrientations)
            .def("setOrientations", &PatchEnergyJITUnion::setOrientations)
            .def("getCharges", &PatchEnergyJITUnion::getCharges)
            .def("setCharges", &PatchEnergyJITUnion::setCharges)
            .def("getDiameters", &PatchEnergyJITUnion::getDiameters)
            .def("setDiameters", &PatchEnergyJITUnion::setDiameters)
            .def("getTypeids", &PatchEnergyJITUnion::getTypeids)
            .def("setTypeids", &PatchEnergyJITUnion::setTypeids)
            .def_property("leaf_capacity", &PatchEnergyJITUnion::getLeafCapacity, &PatchEnergyJITUnion::setLeafCapacity)
            .def_property("r_cut_union", &PatchEnergyJITUnion::getRCutUnion, &PatchEnergyJITUnion::setRCutUnion)
            .def_property_readonly("array_size_union", &PatchEnergyJITUnion::getArraySizeUnion)
            .def_property_readonly("alpha_union",&PatchEnergyJITUnion::getAlphaUnionNP)
            ;
    }