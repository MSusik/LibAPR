//
// Created by cheesema on 13.02.18.
//

#ifndef LIBAPR_APRTREE_HPP
#define LIBAPR_APRTREE_HPP

#include "src/data_structures/APR/APR.hpp"

template<typename imageType>
class APRTree {
public:
    APRTree(APR<imageType>& apr){
        initialize_apr_tree(apr);
    }

private:
    APRAccess tree_access;

    void initialize_apr_tree(APR<imageType>& apr){

        APRIterator<imageType> apr_iterator(apr);

        ///////////////////////
        ///
        /// Set the iterator using random access by particle cell spatial index (x,y,z) and particle cell level
        ///
        ////////////////////////
        std::vector<MeshData<uint8_t>> particle_cell_parent_tree;
        uint64_t l_max = apr.level_max()-1;
        uint64_t l_min = apr.level_min()-1; // extend one extra level

        tree_access.level_min = l_min;
        tree_access.level_max = l_max;

        particle_cell_parent_tree.resize(l_max + 1);

        for (uint64_t l = l_min; l < (l_max + 1) ;l ++){
            particle_cell_parent_tree[l].initialize((int)ceil((1.0*apr.orginal_dimensions(0))/pow(2.0,1.0*l_max - l + 1)),
                                                    (int)ceil((1.0*apr.orginal_dimensions(1))/pow(2.0,1.0*l_max - l + 1)),
                                                    (int)ceil((1.0*apr.orginal_dimensions(2))/pow(2.0,1.0*l_max - l + 1)), (uint8_t)0);
        }


        uint64_t counter = 0;

        uint64_t particle_number;
        //Basic serial iteration over all particles
        for (particle_number = 0; particle_number < apr.total_number_particles(); ++particle_number) {
            //This step is required for all loops to set the iterator by the particle number
            apr_iterator.set_iterator_to_particle_by_number(particle_number);

            size_t y_p = apr_iterator.y()/2;
            size_t x_p = apr_iterator.x()/2;
            size_t z_p = apr_iterator.z()/2;

            particle_cell_parent_tree[apr_iterator.level()-1].access_no_protection(y_p,x_p,z_p)=2;

            counter++;

        }

        this->tree_access.initialize_tree_access(apr,particle_cell_parent_tree);

        std::cout << apr.total_number_particles() << std::endl;
        std::cout << tree_access.total_number_particles << std::endl;
        std::cout << counter << std::endl;


    }


};


#endif //LIBAPR_APRTREE_HPP