//
// Created by cheesema on 16/03/17.
//

#ifndef PARTPLAY_APR_HPP
#define PARTPLAY_APR_HPP

#include "src/data_structures/Tree/PartCellStructure.hpp"

#include "src/data_structures/Tree/PartCellParent.hpp"
#include "src/numerics/filter_numerics.hpp"
#include "src/numerics/misc_numerics.hpp"

#include "src/numerics/filter_help/CurrLevel.hpp"

#include "src/io/hdf5functions_blosc.h"

#include "src/data_structures/APR/APR_iterator.hpp"

#include <map>
#include <unordered_map>


typedef std::unordered_map<uint16_t,uint16_t> hash_map;
//typedef std::map<uint16_t,uint16_t> hash_map;

template<typename ImageType>
class APR : public APR_iterator<ImageType>{

public:

    ParticleDataNew<ImageType, uint64_t> part_new;
    //flattens format to particle = cell, this is in the classic access/part paradigm

    ExtraPartCellData<uint16_t> y_vec;

    ExtraPartCellData<ImageType> particles_int;

    PartCellData<uint64_t> pc_data;

    std::vector<float> num_parts;

    std::vector<float> num_elements;

    ExtraPartCellData<uint64_t> num_parts_xy;

    ExtraPartCellData<hash_map> random_access;

    std::string name;

    Proc_par pars;

    double num_elements_total;

    APR(){
        this->pc_data_pointer = &pc_data;
    }

    APR(PartCellStructure<float,uint64_t>& pc_struct){
        init(pc_struct);
        this->pc_data_pointer = &pc_data;
    }

    void init(PartCellStructure<float,uint64_t>& pc_struct){
        part_new.initialize_from_structure(pc_struct);

        create_y_data();

        part_new.create_particles_at_cell_structure(particles_int);

        part_new.create_pc_data_new(pc_data);

        shift_particles_from_cells(particles_int);

        part_new.initialize_from_structure(pc_struct);


    }


    void init_by_part_iteration(APR_iterator<float>& apr_it){
        //
        //  Initializes the required datastructures for by particles and parralell iteration
        //

        get_part_numbers();
        set_part_numbers_xz();

        apr_it.num_parts = &this->num_parts;
        apr_it.num_parts_xz_pointer = &this->num_parts_xy;
        apr_it.num_parts_total = this->num_parts_total;
        apr_it.pc_data_pointer = &pc_data;

        apr_it.curr_level.init(pc_data);
    }

    void init_iterator(APR_iterator<float>& apr_it){
        //
        //  Initializes the required datastructures for by particles and parralell iteration
        //

        apr_it.num_parts = &this->num_parts;
        apr_it.num_parts_xz_pointer = &this->num_parts_xy;
        apr_it.num_parts_total = this->num_parts_total;
        apr_it.pc_data_pointer = &pc_data;

        apr_it.curr_level.init(pc_data);
    }



    void init_cells(PartCellStructure<float,uint64_t>& pc_struct){
        create_pc_data_new(pc_struct);

        this->curr_level.init(pc_data);

    }

    void init_random_access(){


        random_access.initialize_structure_parts_empty(particles_int);

        ExtraPartCellData<std::pair<uint16_t,uint16_t>> hash_init;

        hash_init.initialize_structure_parts_empty(particles_int);

        int counter = 0;

        //create the intiializer lists

        //loop over all particles
        for (this->begin(); this->end() == true; this->it_forward()) {

            hash_init.data[this->depth()][this->curr_level.pc_offset].push_back({this->y(),this->j()});

        }

        //now create the actual hash tables
        for(uint64_t i = pc_data.depth_min;i <= pc_data.depth_max;i++) {

            const unsigned int x_num_ = pc_data.x_num[i];
            const unsigned int z_num_ = pc_data.z_num[i];

            for (uint64_t z_ = 0; z_ < z_num_; z_++) {

                for (uint64_t x_ = 0; x_ < x_num_; x_++) {
                    const uint64_t offset_pc_data = x_num_ * z_ + x_;
                    if(hash_init.data[i][offset_pc_data].size() > 0) {
                        random_access.data[i][offset_pc_data].resize(1);

                        random_access.data[i][offset_pc_data][0].insert(hash_init.data[i][offset_pc_data].begin(),
                                                                        hash_init.data[i][offset_pc_data].end());
                    }

                }
            }
        }

    }






    void init_pc_data(){


        part_new.create_pc_data_new(pc_data);

        pc_data.org_dims = y_vec.org_dims;
    }

    void create_y_data(){
        //
        //  Bevan Cheeseman 2017
        //
        //  Creates y index
        //

        y_vec.initialize_structure_parts(part_new.particle_data);

        y_vec.org_dims = part_new.access_data.org_dims;

        int z_,x_,j_,y_;

        for(uint64_t depth = (part_new.access_data.depth_min);depth <= part_new.access_data.depth_max;depth++) {
            //loop over the resolutions of the structure
            const unsigned int x_num_ = part_new.access_data.x_num[depth];
            const unsigned int z_num_ = part_new.access_data.z_num[depth];

            CurrentLevel<ImageType, uint64_t> curr_level(part_new);
            curr_level.set_new_depth(depth, part_new);

            const float step_size = pow(2,curr_level.depth_max - curr_level.depth);

#pragma omp parallel for default(shared) private(z_,x_,j_) firstprivate(curr_level) if(z_num_*x_num_ > 100)
            for (z_ = 0; z_ < z_num_; z_++) {
                //both z and x are explicitly accessed in the structure

                for (x_ = 0; x_ < x_num_; x_++) {

                    curr_level.set_new_xz(x_, z_, part_new);

                    int counter = 0;

                    for (j_ = 0; j_ < curr_level.j_num; j_++) {

                        bool iscell = curr_level.new_j(j_, part_new);

                        if (iscell) {
                            //Indicates this is a particle cell node
                            curr_level.update_cell(part_new);

                            y_vec.data[depth][curr_level.pc_offset][counter] = curr_level.y;

                            counter++;
                        } else {

                            curr_level.update_gap();

                        }


                    }
                }
            }
        }



    }

    template<typename U>
    void shift_particles_from_cells(ExtraPartCellData<U>& pdata_old){
        //
        //  Bevan Cheesean 2017
        //
        //  Transfers them to align with the part data, to align with particle data no gaps
        //
        //

        ExtraPartCellData<U> pdata_new;

        pdata_new.initialize_structure_cells(pc_data);

        uint64_t z_,x_,j_,node_val;
        uint64_t part_offset;

        for(uint64_t i = pc_data.depth_min;i <= pc_data.depth_max;i++){

            const unsigned int x_num_ = pc_data.x_num[i];
            const unsigned int z_num_ = pc_data.z_num[i];

#pragma omp parallel for default(shared) private(z_,x_,j_,node_val)  if(z_num_*x_num_ > 100)
            for(z_ = 0;z_ < z_num_;z_++){

                for(x_ = 0;x_ < x_num_;x_++){
                    const size_t offset_pc_data = x_num_*z_ + x_;
                    const size_t j_num = pc_data.data[i][offset_pc_data].size();

                    int counter = 0;

                    for(j_ = 0; j_ < j_num;j_++){
                        //raster over both structures, generate the index for the particles, set the status and offset_y_coord diff

                        node_val = pc_data.data[i][offset_pc_data][j_];

                        if(!(node_val&1)){

                            pdata_new.data[i][offset_pc_data][counter] = pdata_old.data[i][offset_pc_data][j_];

                            counter++;

                        } else {

                        }

                    }

                    pdata_new.data[i][offset_pc_data].resize(counter); //reduce to new size
                }
            }
        }

        std::swap(pdata_new,pdata_old);

    }

    uint64_t begin(){

        return this->curr_level.init_iterate(pc_data);

    }

    uint64_t begin(unsigned int depth){
        return this->curr_level.init_iterate(pc_data,depth);
    }

    uint64_t end(){
        return this->curr_level.counter > 0;
    }

//    uint64_t end(unsigned int depth){
//
//    }

    uint64_t it_forward(){

        this->curr_level.move_to_next_pc(pc_data);

        return this->curr_level.counter;
    }

    uint64_t it_forward(unsigned int depth){

        this->curr_level.move_to_next_pc(pc_data,depth);

        return this->curr_level.counter;
    }



    ////////////////////////
    //
    //  Accessing info when iterating. Does not make sense outisde of a looping structure
    //
    //////////////////////////





    template<typename U,typename V>
    void interp_img(Mesh_data<U>& img,ExtraPartCellData<V>& parts){
        //
        //  Bevan Cheeseman 2016
        //
        //  Takes in a APR and creates piece-wise constant image
        //

        img.initialize(pc_data.org_dims[0],pc_data.org_dims[1],pc_data.org_dims[2],0);

        int z_,x_,j_,y_;

        for(uint64_t depth = (pc_data.depth_min);depth <= pc_data.depth_max;depth++) {
            //loop over the resolutions of the structure
            const unsigned int x_num_ = pc_data.x_num[depth];
            const unsigned int z_num_ = pc_data.z_num[depth];

            const unsigned int x_num_min_ = 0;
            const unsigned int z_num_min_ = 0;

            CurrentLevel<float, uint64_t> curr_level_l(pc_data);
            curr_level_l.set_new_depth(depth, pc_data);

            const float step_size = pow(2,curr_level_l.depth_max - curr_level_l.depth);

#pragma omp parallel for default(shared) private(z_,x_,j_) firstprivate(curr_level_l) if(z_num_*x_num_ > 100)
            for (z_ = z_num_min_; z_ < z_num_; z_++) {
                //both z and x are explicitly accessed in the structure

                for (x_ = x_num_min_; x_ < x_num_; x_++) {

                    curr_level_l.set_new_xz(x_, z_, pc_data);

                    for (j_ = 0; j_ < curr_level_l.j_num; j_++) {

                        bool iscell = curr_level_l.new_j(j_, pc_data);

                        if (iscell) {
                            //Indicates this is a particle cell node
                            curr_level_l.update_cell(pc_data);

                            int dim1 = curr_level_l.y * step_size;
                            int dim2 = curr_level_l.x * step_size;
                            int dim3 = curr_level_l.z * step_size;

                            float temp_int;
                            //add to all the required rays

                            temp_int = curr_level_l.get_val(parts);

                            const int offset_max_dim1 = std::min((int) img.y_num, (int) (dim1 + step_size));
                            const int offset_max_dim2 = std::min((int) img.x_num, (int) (dim2 + step_size));
                            const int offset_max_dim3 = std::min((int) img.z_num, (int) (dim3 + step_size));

                            for (int q = dim3; q < offset_max_dim3; ++q) {

                                for (int k = dim2; k < offset_max_dim2; ++k) {
#pragma omp simd
                                    for (int i = dim1; i < offset_max_dim1; ++i) {
                                        img.mesh[i + (k) * img.y_num + q*img.y_num*img.x_num] = temp_int;
                                    }
                                }
                            }


                        } else {

                            curr_level_l.update_gap(pc_data);

                        }


                    }
                }
            }
        }




    }


    template<typename U>
    void interp_depth(Mesh_data<U>& img){

        //get depth
        ExtraPartCellData<U> depth_parts;
        depth_parts.initialize_structure_cells(pc_data);

        for (begin(); end() == true ; it_forward()) {
            //
            //  Demo APR iterator
            //

            //access and info
            this->curr_level.get_val(depth_parts) = this->depth();

        }

        interp_img(img,depth_parts);


    }

    template<typename U>
    void interp_type(Mesh_data<U>& img){

        //get depth
        ExtraPartCellData<U> type_parts;
        type_parts.initialize_structure_cells(pc_data);

        for (begin(); end() == true ; it_forward()) {
            //
            //  Demo APR iterator
            //

            //access and info
            this->curr_level.get_val(type_parts) = this->type();

        }

        interp_img(img,type_parts);


    }


    template<typename U,typename V>
    void interp_parts_smooth(Mesh_data<U>& out_image,ExtraPartCellData<V>& interp_data,std::vector<float> scale_d = {1,1,1}){
        //
        //  Performs a smooth interpolation, based on the depth (level l) in each direction.
        //

        Part_timer timer;
        timer.verbose_flag = true;

        Mesh_data<U> pc_image;
        Mesh_data<uint8_t> k_img;

        interp_img(pc_image,interp_data);

        interp_depth(k_img);

        int filter_offset = 0;

        unsigned int x_num = pc_image.x_num;
        unsigned int y_num = pc_image.y_num;
        unsigned int z_num = pc_image.z_num;

        Mesh_data<U> output_data;
        output_data.initialize((int)y_num,(int)x_num,(int)z_num,0);

        std::vector<U> temp_vec;
        temp_vec.resize(y_num,0);

        uint64_t offset_min;
        uint64_t offset_max;

        uint64_t j = 0;
        uint64_t k = 0;
        uint64_t i = 0;

        float factor = 0;

        int k_max = pc_data.depth_max;

        timer.start_timer("x direction");

        std::copy(k_img.mesh.begin(),k_img.mesh.end(),output_data.mesh.begin());

#pragma omp parallel for default(shared) private(j,i,k,offset_min,offset_max,filter_offset,factor)
        for(j = 0; j < z_num;j++){
            for(i = 0; i < x_num;i++){

                for(k = 0;k < y_num;k++){

                    filter_offset = floor(pow(2,k_max - output_data.mesh[j*x_num*y_num + i*y_num + k])/scale_d[0]);

                    offset_max = std::min((int)(k + filter_offset),(int)(y_num-1));
                    offset_min = std::max((int)(k - filter_offset),(int)0);

                    factor = 1.0/(offset_max - offset_min+1);

                    uint64_t f = 0;
                    output_data.mesh[j*x_num*y_num + i*y_num + k] = 0;
                    for(uint64_t c = offset_min;c <= offset_max;c++){

                        output_data.mesh[j*x_num*y_num + i*y_num + k] += pc_image.mesh[j*x_num*y_num + i*y_num + c]*factor;

                    }

                }
            }
        }

        timer.stop_timer();

        timer.start_timer("y direction");

        std::swap(output_data.mesh,pc_image.mesh);

        std::copy(k_img.mesh.begin(),k_img.mesh.end(),output_data.mesh.begin());

#pragma omp parallel for default(shared) private(j,i,k,offset_min,offset_max,filter_offset,factor)
        for(j = 0; j < z_num;j++){
            for(i = 0; i < x_num;i++){

                for(k = 0;k < y_num;k++){

                    filter_offset = floor(pow(2,k_max - output_data.mesh[j*x_num*y_num + i*y_num + k])/scale_d[1]);

                    offset_max = std::min((int)(i + filter_offset),(int)(x_num-1));
                    offset_min = std::max((int)(i - filter_offset),(int)0);

                    factor = 1.0/(offset_max - offset_min+1);

                    uint64_t f = 0;
                    output_data.mesh[j*x_num*y_num + i*y_num + k] = 0;
                    for(uint64_t c = offset_min;c <= offset_max;c++){

                        output_data.mesh[j*x_num*y_num + i*y_num + k] += pc_image.mesh[j*x_num*y_num + c*y_num + k]*factor;
                    }

                }
            }
        }

//
//
//    // z loop
//

        timer.stop_timer();

        std::swap(output_data.mesh,pc_image.mesh);

        timer.start_timer("z direction");

        std::copy(k_img.mesh.begin(),k_img.mesh.end(),output_data.mesh.begin());

#pragma omp parallel for default(shared) private(j,i,k,offset_min,offset_max,filter_offset,factor)
        for(j = 0; j < z_num;j++){
            for(i = 0; i < x_num;i++){


                for(k = 0;k < y_num;k++){

                    filter_offset = floor(pow(2,k_max - output_data.mesh[j*x_num*y_num + i*y_num + k])/scale_d[2]);

                    offset_max = std::min((int)(j + filter_offset),(int)(z_num-1));
                    offset_min = std::max((int)(j - filter_offset),(int)0);

                    factor = 1.0/(offset_max - offset_min+1);

                    uint64_t f = 0;
                    output_data.mesh[j*x_num*y_num + i*y_num + k]=0;
                    for(uint64_t c = offset_min;c <= offset_max;c++){

                        output_data.mesh[j*x_num*y_num + i*y_num + k] += pc_image.mesh[c*x_num*y_num + i*y_num + k]*factor;

                    }

                }
            }
        }

        timer.stop_timer();

        out_image = output_data;

    }



    template<typename U,typename V>
    void get_parts_from_img(Mesh_data<U>& img,ExtraPartCellData<V>& parts){
        //
        //  Bevan Cheeseman 2016
        //
        //  Samples particles from an image using the nearest pixel (rounded up, i.e. next pixel after particles that sit on off pixel locations)
        //

        parts.initialize_structure_cells(pc_data);

        int z_,x_,j_,y_;

        for(uint64_t depth = (pc_data.depth_min);depth <= pc_data.depth_max;depth++) {
            //loop over the resolutions of the structure
            const unsigned int x_num_ = pc_data.x_num[depth];
            const unsigned int z_num_ = pc_data.z_num[depth];

            const unsigned int x_num_min_ = 0;
            const unsigned int z_num_min_ = 0;

            CurrentLevel<float, uint64_t> curr_level_l(pc_data);
            curr_level_l.set_new_depth(depth, pc_data);

            const float step_size = pow(2,curr_level_l.depth_max - curr_level_l.depth);

#pragma omp parallel for default(shared) private(z_,x_,j_) firstprivate(curr_level_l) if(z_num_*x_num_ > 100)
            for (z_ = z_num_min_; z_ < z_num_; z_++) {
                //both z and x are explicitly accessed in the structure

                for (x_ = x_num_min_; x_ < x_num_; x_++) {

                    curr_level_l.set_new_xz(x_, z_, pc_data);

                    for (j_ = 0; j_ < curr_level_l.j_num; j_++) {

                        bool iscell = curr_level_l.new_j(j_, pc_data);

                        if (iscell) {
                            //Indicates this is a particle cell node
                            curr_level_l.update_cell(pc_data);

                            int dim1 = floor((curr_level_l.y+0.5) * step_size);
                            int dim2 = floor((curr_level_l.x+0.5) * step_size);
                            int dim3 = floor((curr_level_l.z+0.5) * step_size);

                            curr_level_l.get_val(parts) = img(dim1,dim2,dim3);

                        } else {

                            curr_level_l.update_gap(pc_data);

                        }


                    }
                }
            }
        }




    }

    template<typename U,typename V>
    void get_parts_from_img(std::vector<Mesh_data<U>>& img_by_level,ExtraPartCellData<V>& parts){
        //
        //  Bevan Cheeseman 2016
        //
        //  Samples particles from an image using an image tree (img_by_level is a vector of images)
        //

        parts.initialize_structure_cells(pc_data);

        //initialization of the iteration structures
        APR_iterator<float> apr_it(*this); //this is required for parallel access
        uint64_t part;

#pragma omp parallel for schedule(static) private(part) firstprivate(apr_it)
        for (part = 0; part < this->num_parts_total; ++part) {
            //needed step for any parallel loop (update to the next part)
            apr_it.set_part(part);

            apr_it(parts) = img_by_level[apr_it.depth()](apr_it.y(),apr_it.x(),apr_it.z());

            if(apr_it(parts)==0){
                int stop = 1;
                std::cout << "broke" << std::endl;
            }

        }


//        int z_,x_,j_,y_;
//
//        for(uint64_t depth = (pc_data.depth_min);depth <= pc_data.depth_max;depth++) {
//            //loop over the resolutions of the structure
//            const unsigned int x_num_ = pc_data.x_num[depth];
//            const unsigned int z_num_ = pc_data.z_num[depth];
//
//            const unsigned int x_num_min_ = 0;
//            const unsigned int z_num_min_ = 0;
//
//            CurrentLevel<float, uint64_t> curr_level_l(pc_data);
//            curr_level_l.set_new_depth(depth, pc_data);
//
//            const float step_size = pow(2,curr_level_l.depth_max - curr_level_l.depth);
//
//#pragma omp parallel for default(shared) private(z_,x_,j_) firstprivate(curr_level_l) if(z_num_*x_num_ > 100)
//            for (z_ = z_num_min_; z_ < z_num_; z_++) {
//                //both z and x are explicitly accessed in the structure
//
//                for (x_ = x_num_min_; x_ < x_num_; x_++) {
//
//                    curr_level_l.set_new_xz(x_, z_, pc_data);
//
//                    for (j_ = 0; j_ < curr_level_l.j_num; j_++) {
//
//                        bool iscell = curr_level_l.new_j(j_, pc_data);
//
//                        if (iscell) {
//                            //Indicates this is a particle cell node
//                            curr_level_l.update_cell(pc_data);
//
//                            curr_level_l.get_val(parts) = img_by_level[depth](curr_level_l.y,curr_level_l.x,curr_level_l.z);
//
//                        } else {
//
//                            curr_level_l.update_gap(pc_data);
//
//                        }
//
//
//                    }
//                }
//            }
//        }




    }


    void get_part_numbers() {
        //
        //  Computes totals of total number of particles, and the total number of elements (PC and gap nodes)
        //

        this->num_parts.resize(pc_data.depth_max + 1);
        this->num_elements.resize(pc_data.depth_max + 1);

        int z_, x_, j_, y_;

        uint64_t counter_parts = 0;
        uint64_t counter_elements = 0;

        for (uint64_t depth = (pc_data.depth_min); depth <= pc_data.depth_max; depth++) {
            //loop over the resolutions of the structure
            const unsigned int x_num_ = pc_data.x_num[depth];
            const unsigned int z_num_ = pc_data.z_num[depth];

            const unsigned int x_num_min_ = 0;
            const unsigned int z_num_min_ = 0;

            CurrentLevel<float, uint64_t> curr_level_l(pc_data);
            curr_level_l.set_new_depth(depth, pc_data);

            const float step_size = pow(2, curr_level_l.depth_max - curr_level_l.depth);


            for (z_ = z_num_min_; z_ < z_num_; z_++) {
                //both z and x are explicitly accessed in the structure

                for (x_ = x_num_min_; x_ < x_num_; x_++) {

                    curr_level_l.set_new_xz(x_, z_, pc_data);

                    for (j_ = 0; j_ < curr_level_l.j_num; j_++) {

                        bool iscell = curr_level_l.new_j(j_, pc_data);

                        counter_elements++;

                        if (iscell) {
                            //Indicates this is a particle cell node
                            curr_level_l.update_cell(pc_data);

                            counter_parts++;

                        } else {

                            curr_level_l.update_gap(pc_data);

                        }


                    }
                }
            }

            this->num_parts[depth] = counter_parts;
            this->num_elements[depth] = counter_elements;

        }

        this->num_parts_total = 0;
        num_elements_total = 0;

        for (int i = 0; i <= pc_data.depth_max; ++i) {

            num_elements_total += num_elements[i];
        }

        this->num_parts_total += num_parts[pc_data.depth_max];

    }

    void set_part_numbers_xz() {
        //
        //  Computes totals of total number of particles in each xz
        //

        num_parts_xy.initialize_structure_parts_empty(particles_int);

        int z_, x_, j_, y_;

        uint64_t counter_parts = 0;

        for (uint64_t depth = (pc_data.depth_min); depth <= pc_data.depth_max; depth++) {
            //loop over the resolutions of the structure
            const unsigned int x_num_ = pc_data.x_num[depth];
            const unsigned int z_num_ = pc_data.z_num[depth];

            const unsigned int x_num_min_ = 0;
            const unsigned int z_num_min_ = 0;

            CurrentLevel<float, uint64_t> curr_level_l(pc_data);
            curr_level_l.set_new_depth(depth, pc_data);

            const float step_size = pow(2, curr_level_l.depth_max - curr_level_l.depth);



            for (z_ = z_num_min_; z_ < z_num_; z_++) {
                //both z and x are explicitly accessed in the structure

                for (x_ = x_num_min_; x_ < x_num_; x_++) {

                    curr_level_l.set_new_xz(x_, z_, pc_data);



                    for (j_ = 0; j_ < curr_level_l.j_num; j_++) {

                        bool iscell = curr_level_l.new_j(j_, pc_data);


                        if (iscell) {
                            //Indicates this is a particle cell node
                            curr_level_l.update_cell(pc_data);

                            counter_parts++;

                        } else {

                            curr_level_l.update_gap(pc_data);

                        }


                    }

                    num_parts_xy.data[curr_level_l.depth][curr_level_l.pc_offset].push_back(counter_parts);

                }
            }

        }

    }


        template<typename U,typename V>
    void create_pc_data_new(PartCellStructure<U,V>& pc_struct){
        //
        //
        //  Moves from the old data structure to the new datastructure (PC = particles, stores V)
        //
        //

        pc_data.org_dims = pc_struct.org_dims;

        pc_data.y_num = pc_struct.y_num;

        //first add the layers
        pc_data.depth_max = pc_struct.depth_max + 1;
        pc_data.depth_min = pc_struct.depth_min;

        pc_data.z_num.resize(pc_data.depth_max+1);
        pc_data.x_num.resize(pc_data.depth_max+1);

        pc_data.y_num.resize(pc_data.depth_max+1);

        pc_data.y_num[pc_data.depth_max] = pc_struct.org_dims[0];

        pc_data.data.resize(pc_data.depth_max+1);

        for(uint64_t i = pc_data.depth_min;i < pc_data.depth_max;i++){
            pc_data.z_num[i] = pc_struct.z_num[i];
            pc_data.x_num[i] = pc_struct.x_num[i];
            pc_data.y_num[i] = pc_struct.y_num[i];
            pc_data.data[i].resize(pc_struct.z_num[i]*pc_struct.x_num[i]);
        }

        pc_data.z_num[pc_data.depth_max] = pc_struct.org_dims[2];
        pc_data.x_num[pc_data.depth_max] = pc_struct.org_dims[1];
        pc_data.y_num[pc_data.depth_max] = pc_struct.org_dims[0];
        pc_data.data[pc_data.depth_max].resize(pc_data.z_num[pc_data.depth_max]*pc_data.x_num[pc_data.depth_max]);

        particles_int.org_dims = pc_struct.org_dims;

        particles_int.y_num = pc_struct.y_num;

        //first add the layers
        particles_int.depth_max = pc_struct.depth_max + 1;
        particles_int.depth_min = pc_struct.depth_min;

        particles_int.z_num.resize(pc_data.depth_max+1);
        particles_int.x_num.resize(pc_data.depth_max+1);

        particles_int.y_num.resize(pc_data.depth_max+1);

        particles_int.y_num[pc_data.depth_max] = pc_struct.org_dims[0];

        particles_int.data.resize(pc_data.depth_max+1);

        for(uint64_t i = particles_int.depth_min;i <= particles_int.depth_max;i++){
            particles_int.z_num[i] = pc_data.z_num[i];
            particles_int.x_num[i] = pc_data.x_num[i];
            particles_int.y_num[i] = pc_data.y_num[i];
            particles_int.data[i].resize(pc_data.z_num[i]*pc_data.x_num[i]);
        }

        //now initialize the entries of the two data sets, access structure

        //initialize loop variables
        int x_;
        int z_;
        int y_;

        int x_seed;
        int z_seed;
        int y_seed;

        uint64_t j_;

        uint64_t status;
        uint64_t node_val;
        uint16_t node_val_part;

        //next initialize the entries;
        Part_timer timer;
        timer.verbose_flag = false;

        std::vector<uint16_t> temp_exist;
        std::vector<uint16_t> temp_location;

        std::vector<U> temp_int;

        timer.start_timer("intiialize access data structure");

        for(uint64_t i = pc_data.depth_max;i >= pc_data.depth_min;i--){

            const unsigned int x_num = pc_data.x_num[i];
            const unsigned int z_num = pc_data.z_num[i];


            const unsigned int x_num_seed = pc_data.x_num[i-1];
            const unsigned int z_num_seed = pc_data.z_num[i-1];

            temp_exist.resize(pc_data.y_num[i]);
            temp_location.resize(pc_data.y_num[i]);
            temp_int.resize(pc_data.y_num[i]);

#pragma omp parallel for default(shared) private(j_,z_,x_,y_,node_val,status,z_seed,x_seed,node_val_part) firstprivate(temp_exist,temp_location) if(z_num*x_num > 100)
            for(z_ = 0;z_ < z_num;z_++){

                for(x_ = 0;x_ < x_num;x_++){

                    std::fill(temp_exist.begin(), temp_exist.end(), 0);
                    std::fill(temp_location.begin(), temp_location.end(), 0);

                    std::fill(temp_int.begin(), temp_int.end(), 0);

                    if( i < pc_data.depth_max){
                        //access variables
                        const size_t offset_pc_data = x_num*z_ + x_;
                        const size_t j_num = pc_struct.pc_data.data[i][offset_pc_data].size();

                        y_ = 0;

                        //first loop over
                        for(j_ = 0; j_ < j_num;j_++){
                            //raster over both structures, generate the index for the particles, set the status and offset_y_coord diff

                            node_val = pc_struct.pc_data.data[i][offset_pc_data][j_];
                            node_val_part = pc_struct.part_data.access_data.data[i][offset_pc_data][j_];

                            if(!(node_val&1)){
                                //normal node
                                y_++;
                                //create pindex, and create status (0,1,2,3) and type
                                status = (node_val & STATUS_MASK) >> STATUS_SHIFT;  //need the status masks here, need to move them into the datastructure I think so that they are correctly accessible then to these routines.
                                uint16_t part_offset = (node_val_part & Y_PINDEX_MASK_PARTICLE) >> Y_PINDEX_SHIFT_PARTICLE;

                                if(status > SEED){
                                    temp_exist[y_] = status;
                                    temp_location[y_] = part_offset;
                                }

                            } else {

                                y_ = (node_val & NEXT_COORD_MASK) >> NEXT_COORD_SHIFT;
                                y_--;
                            }
                        }
                    }

                    x_seed = x_/2;
                    z_seed = z_/2;

                    if( i > pc_data.depth_min){
                        //access variables
                        size_t offset_pc_data = x_num_seed*z_seed + x_seed;
                        const size_t j_num = pc_struct.pc_data.data[i-1][offset_pc_data].size();


                        y_ = 0;

                        //first loop over
                        for(j_ = 0; j_ < j_num;j_++){
                            //raster over both structures, generate the index for the particles, set the status and offset_y_coord diff

                            node_val_part = pc_struct.part_data.access_data.data[i-1][offset_pc_data][j_];
                            node_val = pc_struct.pc_data.data[i-1][offset_pc_data][j_];

                            if(!(node_val&1)){
                                //normal node
                                y_++;
                                //create pindex, and create status (0,1,2,3) and type
                                status = (node_val & STATUS_MASK) >> STATUS_SHIFT;  //need the status masks here, need to move them into the datastructure I think so that they are correctly accessible then to these routines.
                                uint16_t part_offset = (node_val_part & Y_PINDEX_MASK_PARTICLE) >> Y_PINDEX_SHIFT_PARTICLE;

                                if(status == SEED){
                                    temp_exist[2*y_] = status;
                                    temp_exist[2*y_+1] = status;

                                    temp_location[2*y_] = part_offset + (z_&1)*4 + (x_&1)*2;
                                    temp_location[2*y_+1] = part_offset + (z_&1)*4 + (x_&1)*2 + 1;

                                }

                            } else {

                                y_ = (node_val & NEXT_COORD_MASK) >> NEXT_COORD_SHIFT;
                                y_--;
                            }
                        }
                    }


                    size_t first_empty = 0;

                    size_t offset_pc_data = x_num*z_ + x_;
                    size_t offset_pc_data_seed = x_num_seed*z_seed + x_seed;
                    size_t curr_index = 0;
                    size_t prev_ind = 0;

                    //first value handle the duplication of the gap node

                    status = temp_exist[0];

                    if((status> 0)){
                        first_empty = 0;
                    } else {
                        first_empty = 1;
                    }

                    size_t part_total= 0;

                    for(y_ = 0;y_ < temp_exist.size();y_++){

                        status = temp_exist[y_];

                        if(status> 0){
                            curr_index+= 1 + prev_ind;
                            prev_ind = 0;
                            part_total++;
                        } else {
                            prev_ind = 1;
                        }
                    }

                    if(curr_index == 0){
                        pc_data.data[i][offset_pc_data].resize(1); //always first adds an extra entry for intialization and extra info
                    } else {
                        pc_data.data[i][offset_pc_data].resize(curr_index + 2 - first_empty,0); //gap node to begin, already finishes with a gap node

                    }



                    curr_index = 0;
                    prev_ind = 1;
                    size_t prev_coord = 0;

                    size_t part_counter=0;

                    pc_data.data[i][offset_pc_data][0] = 1;
                    pc_data.data[i][offset_pc_data].back() = 1;

                    //initialize particles
                    particles_int.data[i][offset_pc_data].resize(pc_data.data[i][offset_pc_data].size());

                    for(y_ = 0;y_ < temp_exist.size();y_++){

                        status = temp_exist[y_];

                        if((status> 0)){

                            curr_index++;

                            //set starting type
                            if(prev_ind == 1){
                                //gap node
                                //set type

                                pc_data.data[i][offset_pc_data][curr_index-1] = TYPE_GAP;
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (((uint64_t)y_) << NEXT_COORD_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= ( prev_coord << PREV_COORD_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << YP_DEPTH_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << YM_DEPTH_SHIFT);
                                //CHANGE HERE

                                //gap node
                               // pc_data.data[i][offset_pc_data][curr_index-1] = 1; //set type to gap
                               // pc_data.data[i][offset_pc_data][curr_index-1] |= ((y_ - prev_coord) << COORD_DIFF_SHIFT_PARTICLE); //set the coordinate difference

                                curr_index++;
                            }
                            prev_coord = y_;
                            //set type


                            pc_data.data[i][offset_pc_data][curr_index-1] = TYPE_PC;

                            //initialize the neighbours to empty (to be over-written later if not the case) (Boundary Conditions)
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << XP_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << XM_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << ZP_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << ZM_DEPTH_SHIFT);

                            pc_data.data[i][offset_pc_data][curr_index-1] |= (status << STATUS_SHIFT);

                            //lastly retrieve the intensities
                            if(status == SEED){
                                //seed from up one level
                                particles_int.data[i][offset_pc_data][curr_index-1] = pc_struct.part_data.particle_data.data[i-1][offset_pc_data_seed][temp_location[y_]];
                            }
                            else {
                                //non seed same level
                                particles_int.data[i][offset_pc_data][curr_index-1] = pc_struct.part_data.particle_data.data[i][offset_pc_data][temp_location[y_]];
                            }

                            part_counter++;


                            prev_ind = 0;
                        } else {
                            //store for setting above
                            if(prev_ind == 0){
                                //prev_coord = y_;
                            }

                            prev_ind = 1;

                        }
                    }


                    int stop = 1;



                }

            }
        }

        timer.stop_timer();


        ///////////////////////////////////
        //
        //  Calculate neighbours
        //
        /////////////////////////////////

        //(+y,-y,+x,-x,+z,-z)
        pc_data.set_neighbor_relationships();

        get_part_numbers();

    }


    void init_from_pulling_scheme(std::vector<Mesh_data<uint8_t>>& layers){
        //
        //
        //  INITIALIZE THE PARTICLE CELL STRUCTURE FORM THE OUTPUT OF THE PULLING SCHEME
        //
        //

        //INITIALIZE THE DOMAIN SIZES

        pc_data.x_num.resize(this->depth_max()+1);
        pc_data.y_num.resize(this->depth_max()+1);
        pc_data.z_num.resize(this->depth_max()+1);

        for(int i = pc_data.depth_min;i < pc_data.depth_max;i++){
            pc_data.x_num[i] = layers[i].x_num;
            pc_data.y_num[i] = layers[i].y_num;
            pc_data.z_num[i] = layers[i].z_num;

        }

        pc_data.y_num[pc_data.depth_max] = pc_data.org_dims[0];
        pc_data.x_num[pc_data.depth_max] = pc_data.org_dims[1];
        pc_data.z_num[pc_data.depth_max] = pc_data.org_dims[2];

        //transfer over data-structure to make the same (re-use of function for read-write)

        std::vector<std::vector<uint8_t>> p_map;
        p_map.resize(pc_data.depth_max);

        for (int k = 0; k < pc_data.depth_max; ++k) {
            std::swap(p_map[k],layers[k].mesh);
        }

        create_partcell_structure(p_map);

    }


    void create_partcell_structure(std::vector<std::vector<uint8_t>>& p_map){
        //
        //  Bevan Cheeseman 2017
        //
        //  Takes an optimal part_map configuration from the pushing scheme and creates an efficient data structure for procesing using V, instead of V_n as in original (needs to be optimized)
        //

        //initialize the structure
        pc_data.data.resize(pc_data.depth_max + 1);

        for(uint64_t i = pc_data.depth_min;i <= pc_data.depth_max;i++){

            pc_data.data[i].resize(pc_data.z_num[i]*pc_data.x_num[i]);
        }

        Part_timer timer;
        timer.verbose_flag = true;

        //initialize loop variables
        uint64_t x_;
        uint64_t z_;
        uint64_t y_;

        //next initialize the entries;

        uint64_t curr_index;
        uint64_t status;
        uint64_t prev_ind = 0;

        std::vector<unsigned int> x_num = pc_data.x_num;
        std::vector<unsigned int> y_num = pc_data.y_num;
        std::vector<unsigned int> z_num = pc_data.z_num;

        std::vector<uint64_t> status_temp;

        uint64_t prev_coord = 0;

        timer.start_timer("intiialize part_cells");

        for(uint64_t i = pc_data.depth_max;i >= pc_data.depth_min;i--){

            const unsigned int x_num_ = x_num[i];
            const unsigned int z_num_ = z_num[i];
            const unsigned int y_num_ = y_num[i];

#pragma omp parallel for schedule(dynamic) default(shared) private(z_,x_,y_,curr_index,status,prev_ind) firstprivate(status_temp) if(z_num_*x_num_ > 100)
            for(z_ = 0;z_ < z_num_;z_++){

                for(x_ = 0;x_ < x_num_;x_++){

                    status_temp.resize(y_num_,0);
                    std::fill(status_temp.begin(), status_temp.end(), 0);

                    //interpolate filler and boundaries
                    if(i < pc_data.depth_max){
                        uint64_t offset_part_map = x_*y_num[i] + z_*y_num[i]*x_num[i];

                        for(y_ = 0;y_ < y_num[i];y_++) {

                            status = p_map[i][offset_part_map + y_];

                            if ( status == NEIGHBOURSTATUS) {
                                status_temp[ y_] = BOUNDARY;
                            } else if(status == SLOPESTATUS) {
                                status_temp[ y_] = FILLER;
                            }
                        }
                    }

                    //interpolate seed
                    if((i > pc_data.depth_min) && (i <= pc_data.depth_max)){

                        uint64_t offs = (x_/2)*y_num[i-1] + (z_/2)*y_num[i-1]*x_num[i-1];

                        for(y_ = 0;y_ < y_num[i-1];y_++) {

                            status = p_map[i-1][offs + y_];

                            if (status == SEED) {

                                status_temp[2 * y_] = SEED;
                                status_temp[2 * y_ + 1] = SEED;
                            }
                        }

                    }


                    size_t first_empty = 0;

                    const uint64_t offset_pc_data = x_num_*z_ + x_;
                    curr_index = 0;
                    prev_ind = 0;

                    //first value handle the duplication of the gap node

                    status =  status_temp[0];
                    if((status> 0)){
                        first_empty = 0;
                    } else {
                        first_empty = 1;
                    }

                    for(y_ = 0;y_ < (y_num_);y_++){

                        status = status_temp[y_];

                        if(status> 0){
                            curr_index+= 1 + prev_ind;
                            prev_ind = 0;
                        } else {
                            prev_ind = 1;
                        }
                    }

                    if(curr_index == 0){
                        pc_data.data[i][offset_pc_data].resize(1,0); //always first adds an extra entry for intialization and extra info
                    } else {

                        pc_data.data[i][offset_pc_data].resize(curr_index + 2 - first_empty,0); //gap node to begin, already finishes with a gap node

                    }

                    curr_index = 0;
                    prev_ind = 1;
                    prev_coord = 0;

                    //initialize the first values type
                    pc_data.data[i][offset_pc_data][0] = TYPE_GAP_END;

                    for(y_ = 0;y_ < y_num_;y_++){

                        status = status_temp[y_];

                        if(status> 0){

                            curr_index++;

                            //set starting type
                            if(prev_ind == 1){
                                //gap node
                                //set type
                                pc_data.data[i][offset_pc_data][curr_index-1] = TYPE_GAP;
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (y_ << NEXT_COORD_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= ( prev_coord << PREV_COORD_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << YP_DEPTH_SHIFT);
                                pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << YM_DEPTH_SHIFT);

                                curr_index++;
                            }
                            prev_coord = y_;
                            //set type
                            pc_data.data[i][offset_pc_data][curr_index-1] = TYPE_PC;

                            //initialize the neighbours to empty (to be over-written later if not the case) (Boundary Conditions)
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << XP_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << XM_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << ZP_DEPTH_SHIFT);
                            pc_data.data[i][offset_pc_data][curr_index-1] |= (NO_NEIGHBOUR << ZM_DEPTH_SHIFT);

                            //set the status
                            switch(status){
                                case SEED:
                                {
                                    pc_data.data[i][offset_pc_data][curr_index-1] |= SEED_SHIFTED;
                                    break;
                                }
                                case BOUNDARY:
                                {
                                    pc_data.data[i][offset_pc_data][curr_index-1] |= BOUNDARY_SHIFTED;
                                    break;
                                }
                                case FILLER:
                                {
                                    pc_data.data[i][offset_pc_data][curr_index-1] |= FILLER_SHIFTED;
                                    break;
                                }

                            }

                            prev_ind = 0;
                        } else {
                            //store for setting above
                            if(prev_ind == 0){
                                //prev_coord = y_;
                            }

                            prev_ind = 1;

                        }
                    }

                    //Initialize the last value GAP END indicators to no neighbour
                    pc_data.data[i][offset_pc_data][pc_data.data[i][offset_pc_data].size()-1] = TYPE_GAP_END;
                    pc_data.data[i][offset_pc_data][pc_data.data[i][offset_pc_data].size()-1] |= (NO_NEIGHBOUR << YP_DEPTH_SHIFT);
                    pc_data.data[i][offset_pc_data][pc_data.data[i][offset_pc_data].size()-1] |= (NO_NEIGHBOUR << YM_DEPTH_SHIFT);

                }
            }

        }

        timer.stop_timer();

        ///////////////////////////////////
        //
        //  Calculate neighbours
        //
        /////////////////////////////////

        timer.start_timer("set_up_neigh");

        //(+y,-y,+x,-x,+z,-z)
        pc_data.set_neighbor_relationships();

        timer.stop_timer();

    }

    ///////////////////////
    ///
    /// Random Access Structures (Experimental) Cheeseman 2018
    ///
    ///
    ///////////////////////

    int random_access_pc(uint64_t depth,uint16_t y,uint64_t x,uint64_t z){
        //
        //  Random access check for valid x,z, any given y, returns the index of the stored Particle Intensity.
        //

        int j;

        uint64_t pc_offset = pc_data.x_num[depth]*z + x;

        if(random_access.data[depth][pc_offset].size() > 0) {
            hash_map::iterator pc = random_access.data[depth][pc_offset][0].find(y);

            if(pc != random_access.data[depth][pc_offset][0].end()){
                j = pc->second;
            } else {
                return -1;
            }

        } else {
            return -1;

        }

        return j;

    }

    //////////////////////////
    ///
    /// Experimental random access neighbours.
    ///
    /// \tparam S data type of the particles
    /// \param face the neighbour direction (+y,-y,+x,-x,+z,-z)
    /// \param parts the particles data structure
    /// \param neigh_val vector returning the particles values of the neighbours
    ////////////////////////

    template<typename S>
    void get_neigh_random(unsigned int face,ExtraPartCellData<S>& parts,std::vector<S>& neigh_val){
        //
        //  Get APR face neighbours relying on random access through a map, or unordered map structure for y
        //

        const int8_t dir_y[6] = { 1, -1, 0, 0, 0, 0};
        const int8_t dir_x[6] = { 0, 0, 1, -1, 0, 0};
        const int8_t dir_z[6] = { 0, 0, 0, 0, 1, -1};

        constexpr uint8_t neigh_child_dir[6][3] = {{4,2,2},{4,2,2},{0,4,4},{0,4,4},{0,2,2},{0,2,2}};

        constexpr uint8_t child_offsets[3][3] = {{0,1,1},{1,0,1},{1,1,0}};

        //first try on same depth
        int z_ = this->z() + dir_z[face];
        int x_ = this->x() + dir_x[face];
        int y_ = this->y() + dir_y[face];
        int depth_ = this->depth();

        uint16_t j=0;

        uint64_t pc_offset = pc_data.x_num[depth_]*z_ + x_;
        bool found = false;

        neigh_val.resize(0);

        if((x_ < 0) | (x_ >= pc_data.x_num[depth_]) | (z_ < 0) | (z_ >= pc_data.z_num[depth_]) ){
            //out of bounds
            return;
        }

        if(random_access.data[depth_][pc_offset].size() > 0) {
            hash_map::iterator pc = random_access.data[depth_][pc_offset][0].find(y_);

            if(pc != random_access.data[depth_][pc_offset][0].end()){
                j = pc->second;
                found = true;
            }
        }

        if(!found){
            //
            //  Find parents
            //

            unsigned int depth_p = depth_ - 1;
            unsigned int x_p = x_/2;
            unsigned int y_p = y_/2;
            unsigned int z_p = z_/2;

            pc_offset = pc_data.x_num[depth_p]*z_p + x_p;

            if(random_access.data[depth_p][pc_offset].size() > 0) {
                hash_map::iterator pc = random_access.data[depth_p][pc_offset][0].find(y_p);

                if(pc != random_access.data[depth_p][pc_offset][0].end()){
                    j = pc->second;
                    found = true;
                }
            }

            if(!found) {

                if(depth_ < pc_data.depth_max) {
                    // get the potentially 4 children
                    unsigned int depth_c = depth_ + 1;
                    unsigned int x_c = (x_ + dir_x[face])*2 + (dir_x[face]<0);
                    unsigned int y_c = (y_ + dir_y[face])*2 + (dir_y[face]<0);
                    unsigned int z_c = (z_ + dir_z[face])*2 + (dir_z[face]<0);

                    unsigned int dir = face/2;

                    for (int i = 0; i < 2; ++i) {
                        for (int k = 0; k < 2; ++k) {
                            y_ = y_c + (child_offsets[dir][0])*i + (child_offsets[dir][0])*k;
                            x_ = x_c + (child_offsets[dir][1])*i + (child_offsets[dir][1])*k;
                            z_ = z_c + (child_offsets[dir][2])*i + (child_offsets[dir][2])*k;

                            //add of they exist
                            if((x_ < 0) | (x_ >= pc_data.x_num[depth_c]) | (z_ < 0) | (z_ >= pc_data.z_num[depth_]) ){
                                //out of bounds

                            } else {

                                pc_offset = pc_data.x_num[depth_c]*z_ + x_;

                                if (random_access.data[depth_c][pc_offset].size() > 0) {
                                    hash_map::iterator pc = random_access.data[depth_c][pc_offset][0].find(y_);

                                    if (pc != random_access.data[depth_c][pc_offset][0].end()) {
                                        j = pc->second;
                                        neigh_val.push_back(parts.data[depth_c][pc_offset][j]);
                                    }
                                }

                            }



                        }
                    }



                }

            } else{
                neigh_val.push_back(parts.data[depth_p][pc_offset][j]);
            }

        } else{

            neigh_val.push_back(parts.data[depth_][pc_offset][j]);

        }


    }


    void read_apr(std::string file_name)
    {

        //hdf5 inits
        hid_t fid, pr_groupid, obj_id,attr_id;
        H5G_info_t info;

        //need to register the filters so they work properly
        register_bosc();

        int num_parts,num_cells;

        fid = H5Fopen(file_name.c_str(),H5F_ACC_RDONLY,H5P_DEFAULT);

        //Get the group you want to open

        pr_groupid = H5Gopen2(fid,"ParticleRepr",H5P_DEFAULT);
        H5Gget_info( pr_groupid, &info );

        //Getting an attribute
        obj_id =  H5Oopen_by_idx( fid, "ParticleRepr", H5_INDEX_NAME, H5_ITER_INC,0,H5P_DEFAULT);

        //Load the attributes


        /////////////////////////////////////////////
        //  Get metadata
        //
        //////////////////////////////////////////////

        hid_t       aid, atype, attr;

        aid = H5Screate(H5S_SCALAR);

        name.reserve(100);

        //std::string string_out;

        //std::vector<char> string_out;
        //string_out.resize(80);

        //atype = H5Tcopy (H5T_C_S1);

        char string_out[100];

        for (int j = 0; j < 100; ++j) {
            string_out[j] = 0;
        }

        attr_id = 	H5Aopen(pr_groupid,"name",H5P_DEFAULT);

        atype = H5Aget_type(attr_id);

        hid_t atype_mem = H5Tget_native_type(atype, H5T_DIR_ASCEND);

        H5Aread(attr_id,atype_mem,string_out) ;
        H5Aclose(attr_id);

        name= string_out;
        pars.name = string_out;

        pc_data.org_dims.resize(3);
        pc_data.depth_max = 0;
        pc_data.depth_min = 0;

        attr_id = 	H5Aopen(pr_groupid,"y_num",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_INT,&pc_data.org_dims[0]) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"x_num",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_INT,&pc_data.org_dims[1]) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"z_num",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_INT,&pc_data.org_dims[2]) ;
        H5Aclose(attr_id);

//        attr_id = 	H5Aopen(pr_groupid,"num_parts",H5P_DEFAULT);
//        H5Aread(attr_id,H5T_NATIVE_INT,&num_parts) ;
//        H5Aclose(attr_id);
//
//        attr_id = 	H5Aopen(pr_groupid,"num_cells",H5P_DEFAULT);
//        H5Aread(attr_id,H5T_NATIVE_INT,&num_cells) ;
//        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"depth_max",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_INT,&pc_data.depth_max) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"depth_min",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_INT,&pc_data.depth_min) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"lambda",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.lambda ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"var_th",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.var_th ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"var_th_max",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.var_th_max ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"I_th",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.I_th ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"dx",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.dx ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"dy",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.dy ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"dz",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.dz ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"psfx",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.psfx ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"psfy",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.psfy ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"psfz",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.psfz ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"rel_error",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.rel_error ) ;
        H5Aclose(attr_id);

        attr_id = 	H5Aopen(pr_groupid,"aniso",H5P_DEFAULT);
        H5Aread(attr_id,H5T_NATIVE_FLOAT,&pars.aniso ) ;
        H5Aclose(attr_id);

        //std::cout << "Number particles: " << num_parts << " Number Cells: " << num_cells << std::endl;

        std::vector<std::vector<uint8_t>> p_map_load;
        std::vector<std::vector<uint16_t>> Ip;

        pc_data.depth_max = pc_data.depth_max;

        p_map_load.resize(pc_data.depth_max);
        Ip.resize(pc_data.depth_max+1);

        std::string name;

        pc_data.x_num.resize(pc_data.depth_max+1);
        pc_data.z_num.resize(pc_data.depth_max+1);
        pc_data.y_num.resize(pc_data.depth_max+1);

        for(int i = pc_data.depth_min;i < pc_data.depth_max; i++){

            //get the info

            int x_num;
            name = "p_map_x_num_"+std::to_string(i);

            attr_id = 	H5Aopen(pr_groupid,name.c_str(),H5P_DEFAULT);
            H5Aread(attr_id,H5T_NATIVE_INT,&x_num) ;
            H5Aclose(attr_id);

            int y_num;
            name = "p_map_y_num_"+std::to_string(i);

            attr_id = 	H5Aopen(pr_groupid,name.c_str(),H5P_DEFAULT);
            H5Aread(attr_id,H5T_NATIVE_INT,&y_num) ;
            H5Aclose(attr_id);

            int z_num;
            name = "p_map_z_num_"+std::to_string(i);

            attr_id = 	H5Aopen(pr_groupid,name.c_str(),H5P_DEFAULT);
            H5Aread(attr_id,H5T_NATIVE_INT,&z_num) ;
            H5Aclose(attr_id);


            p_map_load[i].resize(x_num*y_num*z_num);


            if(p_map_load[i].size() > 0){
                name = "p_map_"+std::to_string(i);
                //Load the data then update the particle dataset
                hdf5_load_data(obj_id,H5T_NATIVE_UINT8,p_map_load[i].data(),name.c_str());
            }


            pc_data.x_num[i] = x_num;
            pc_data.y_num[i] = y_num;
            pc_data.z_num[i] = z_num;

        }

        for(int i = pc_data.depth_min;i <= pc_data.depth_max; i++){

            //get the info

            int Ip_num;
            name = "Ip_size_"+std::to_string(i);

            attr_id = 	H5Aopen(pr_groupid,name.c_str(),H5P_DEFAULT);
            H5Aread(attr_id,H5T_NATIVE_INT,&Ip_num);
            H5Aclose(attr_id);


            Ip[i].resize(Ip_num);

            if(Ip[i].size()>0){
                name = "Ip_"+std::to_string(i);
                hdf5_load_data(obj_id,H5T_NATIVE_UINT16,Ip[i].data(),name.c_str());
            }

        }

        pc_data.y_num[pc_data.depth_max] = pc_data.org_dims[0];
        pc_data.x_num[pc_data.depth_max] = pc_data.org_dims[1];
        pc_data.z_num[pc_data.depth_max] = pc_data.org_dims[2];


        //close shiz
        H5Gclose(obj_id);
        H5Gclose(pr_groupid);
        H5Fclose(fid);

        //
        //
        //  Transfer back the intensities
        //
        //
        create_partcell_structure(p_map_load);

        particles_int.initialize_structure_cells(pc_data);

        for (int depth = this->depth_min(); depth <= this->depth_max(); ++depth) {

            uint64_t counter = 0;

            for (this->begin(depth); this->end() == true ; this->it_forward(depth)) {

                this->curr_level.get_val(particles_int) = Ip[depth][counter];

                counter++;

            }
        }


    }

    void write_apr(std::string save_loc,std::string file_name,bool predict = false){
        //
        //
        //  Bevan Cheeseman 2018
        //
        //  Writes the APR to the particle cell structure sparse format, using the p_map for reconstruction
        //
        //

        //initialize
        uint64_t node_val_part;
        uint64_t y_coord;
        int x_;
        int z_;

        uint64_t j_;
        uint64_t status;
        uint64_t curr_key=0;
        uint64_t part_offset=0;


        //Neighbour Routine Checking

        uint64_t p;

        register_bosc();

        std::string hdf5_file_name = save_loc + file_name + "_apr.h5";

        file_name = file_name + "_apr";

        hdf5_create_file(hdf5_file_name);

        //hdf5 inits
        hid_t fid, pr_groupid, obj_id;
        H5G_info_t info;

        hsize_t     dims_out[2];

        hsize_t rank = 1;

        hsize_t dims;
        hsize_t dim_a=1;

        fid = H5Fopen(hdf5_file_name.c_str(),H5F_ACC_RDWR,H5P_DEFAULT);

        //Get the group you want to open

        //////////////////////////////////////////////////////////////////
        //
        //  Write meta-data to the file
        //
        //
        //
        ///////////////////////////////////////////////////////////////////////
        dims = 1;

        pr_groupid = H5Gcreate2(fid,"ParticleRepr",H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
        H5Gget_info( pr_groupid, &info );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"x_num",1,&dims, &pc_data.org_dims[1] );
        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"y_num",1,&dims, &pc_data.org_dims[0] );
        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"z_num",1,&dims, &pc_data.org_dims[2] );


        obj_id = H5Gcreate2(fid,"ParticleRepr/t",H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);

        dims_out[0] = 1;
        dims_out[1] = 1;

        //just an identifier in here for the reading of the parts

        int num_parts = this->num_parts_total;
        int num_cells = this->num_elements_total;

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"num_parts",1,dims_out, &num_parts );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"num_cells",1,dims_out, &num_cells );

        // New parameter and background data

        if(pars.name.size() == 0){
            pars.name = "no_name";
            name = "no_name";
        }

        hdf5_write_string(pr_groupid,"name",pars.name);

        std::string git_hash = exec("git rev-parse HEAD");

        hdf5_write_string(pr_groupid,"githash",git_hash);

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"lambda",1,dims_out, &pars.lambda );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"var_th",1,dims_out, &pars.var_th );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"var_th_max",1,dims_out, &pars.var_th_max );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"I_th",1,dims_out, &pars.I_th );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"dx",1,dims_out, &pars.dx );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"dy",1,dims_out, &pars.dy );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"dz",1,dims_out, &pars.dz );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"psfx",1,dims_out, &pars.psfx );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"psfy",1,dims_out, &pars.psfy );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"psfz",1,dims_out, &pars.psfz );

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"rel_error",1,dims_out, &pars.rel_error);

        hdf5_write_attribute(pr_groupid,H5T_NATIVE_FLOAT,"aniso",1,dims_out, &pars.aniso);

        //////////////////////////////////////////////////////////////////
        //
        //  Write data to the file
        //
        //
        //
        ///////////////////////////////////////////////////////////////////////

        uint64_t depth_min = pc_data.depth_min;

        std::vector<uint16_t> Ip;

        ExtraPartCellData<ImageType> temp_int;
        temp_int.initialize_structure_cells(pc_data);
        temp_int.data = particles_int.data;

        shift_particles_from_cells(temp_int);


        for(uint64_t i = pc_data.depth_min;i <= pc_data.depth_max;i++){

            const unsigned int x_num_ = pc_data.x_num[i];
            const unsigned int z_num_ = pc_data.z_num[i];
            const unsigned int y_num_ = pc_data.y_num[i];

            //write the vals

            Ip.resize(0);

            for(z_ = 0;z_ < z_num_;z_++){

                for(x_ = 0;x_ < x_num_;x_++){

                    const size_t offset_pc_data = x_num_*z_ + x_;

                    const size_t j_num = temp_int.data[i][offset_pc_data].size();

                    uint64_t curr_size = Ip.size();
                    Ip.resize(curr_size+ j_num);

                    std::copy(temp_int.data[i][offset_pc_data].begin(),temp_int.data[i][offset_pc_data].end(),Ip.begin() + curr_size);

                }

            }

            dims = Ip.size();

            std::string name = "Ip_size_"+std::to_string(i);
            hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,name.c_str(),1,&dim_a,&dims);

            if(Ip.size() > 0){
                //write the parts

                name = "Ip_"+std::to_string(i);
                if(predict) {

                    hdf5_write_data_blosc(obj_id, H5T_NATIVE_INT16, name.c_str(), rank, &dims, Ip.data());
                } else {
                    hdf5_write_data_blosc(obj_id, H5T_NATIVE_UINT16, name.c_str(), rank, &dims, Ip.data());
                }
            }

        }


        for(uint64_t i = pc_data.depth_min;i < pc_data.depth_max;i++){


            unsigned int x_num_ = pc_data.x_num[i];
            unsigned int z_num_ = pc_data.z_num[i];
            unsigned int y_num_ = pc_data.y_num[i];

            uint64_t node_val;

            std::vector<uint8_t> p_map;

            p_map.resize(x_num_*z_num_*y_num_,0);

            std::fill(p_map.begin(), p_map.end(), 0);

            //For each depth there are two loops, one for SEED status particles, at depth + 1, and one for BOUNDARY and FILLER CELLS, to ensure contiguous memory access patterns.

            // SEED PARTICLE STATUS LOOP (requires access to three data structures, particle access, particle data, and the part-map)
#pragma omp parallel for default(shared) private(z_,x_,j_,node_val,status,y_coord) if(z_num_*x_num_ > 100)
            for(z_ = 0;z_ < z_num_;z_++){

                for(x_ = 0;x_ < x_num_;x_++){

                    const size_t offset_pc_data = x_num_*z_ + x_;
                    const size_t offset_p_map = y_num_*x_num_*z_ + y_num_*x_;

                    const size_t j_num = pc_data.data[i][offset_pc_data].size();

                    y_coord = 0;

                    for(j_ = 0;j_ < j_num;j_++){

                        node_val = pc_data.data[i][offset_pc_data][j_];

                        if (!(node_val&1)){
                            //get the index gap node
                            y_coord++;

                            status = pc_data.get_status(node_val);

                            if(status > 1) {

                                p_map[offset_p_map + y_coord] = status;

                            }

                        } else {

                            y_coord = (node_val & NEXT_COORD_MASK) >> NEXT_COORD_SHIFT;
                            y_coord--; //set the y_coordinate to the value before the next coming up in the structure

                        }

                    }

                }

            }


            x_num_ = pc_data.x_num[i+1];
            z_num_ = pc_data.z_num[i+1];
            y_num_ = pc_data.y_num[i+1];

            int x_num_d = pc_data.x_num[i];
            int z_num_d = pc_data.z_num[i];
            int y_num_d = pc_data.y_num[i];

#pragma omp parallel for default(shared) private(z_,x_,j_,node_val,status,y_coord) if(z_num_*x_num_ > 100)
            for(z_ = 0;z_ < z_num_;z_++){

                for(x_ = 0;x_ < x_num_;x_++){

                    const size_t offset_pc_data = x_num_*z_ + x_;
                    const size_t offset_p_map = y_num_d*x_num_d*(z_/2) + y_num_d*(x_/2);

                    const size_t j_num = pc_data.data[i+1][offset_pc_data].size();

                    y_coord = 0;

                    for(j_ = 0;j_ < j_num;j_++){

                        node_val = pc_data.data[i+1][offset_pc_data][j_];

                        if (!(node_val&1)){
                            //get the index gap node
                            y_coord++;

                            status = pc_data.get_status(node_val);

                            if(status == 1) {

                                p_map[offset_p_map + y_coord/2] = status;

                            }

                        } else {

                            y_coord = (node_val & NEXT_COORD_MASK) >> NEXT_COORD_SHIFT;
                            y_coord--; //set the y_coordinate to the value before the next coming up in the structure
                        }

                    }

                }

            }


            dims = p_map.size();
            name = "p_map_"+std::to_string(i);
            hdf5_write_data_blosc(obj_id,H5T_NATIVE_UINT8,name.c_str(),rank,&dims, p_map.data());

            name = "p_map_x_num_"+std::to_string(i);
            hsize_t attr = x_num_d;
            hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,name.c_str(),1,&dim_a, &attr);

            attr = y_num_d;
            name = "p_map_y_num_"+std::to_string(i);
            hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,name.c_str(),1,&dim_a, &attr);

            attr = z_num_d;
            name = "p_map_z_num_"+std::to_string(i);
            hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,name.c_str(),1,&dim_a, &attr);

        }

        hsize_t attr = pc_data.depth_min;
        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"depth_max",1,&dim_a, &pc_data.depth_max );
        hdf5_write_attribute(pr_groupid,H5T_NATIVE_INT,"depth_min",1,&dim_a, &attr );

        // output the file size
        hsize_t file_size;
        H5Fget_filesize(fid, &file_size);

        std::cout << "HDF5 Filesize: " << file_size*1.0/1000000.0 << " MB" << std::endl;

        //close shiz
        H5Gclose(obj_id);
        H5Gclose(pr_groupid);
        H5Fclose(fid);

        std::cout << "Writing Complete" << std::endl;


    }

    template<typename S>
    void predict_intensities(ExtraPartCellData<S>& parts){
        //
        //  Reduce the entropy of the saved sequence by predicting the intensity and storing \delta
        //

        //init temp data
        ExtraPartCellData<S> delta;
        delta.initialize_structure_cells(pc_data);

        std::vector<S> neigh_vec;

        std::vector<unsigned int> dir = {1,3,5};

        //loops from lowest level to highest
        for (this->begin(); this->end() == true ; this->it_forward()) {

            //get the minus neighbours (1,3,5)

            float pred = 0;
            float counter = 0;

            for (int j = 0; j < dir.size(); ++j) {

                get_neigh_dir(parts, neigh_vec, dir[j]);



                if (neigh_vec.size() > 0) {
                    //check if the depth is less
                    if (pc_data.pc_key_get_depth(this->curr_level.neigh_part_keys.neigh_face[dir[j]][0]) <= this->depth()) {
                        for (int i = 0; i < neigh_vec.size(); ++i) {
                            pred += neigh_vec[i];
                            counter++;
                        }
                    }
                }
            }

            if(counter > 0){
                pred = floor(pred/counter);
            }

            this->curr_level.get_val(delta) = this->curr_level.get_val(parts) - pred;


        }

        std::swap(parts,delta);

    }


};




#endif //PARTPLAY_APR_HPP
