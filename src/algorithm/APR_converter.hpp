////////////////////////////////
///
/// Bevan Cheeseman 2018
///
/// APR Converter class handles the methods and functions for creating an APR from an input image
///
////////////////////////////////

#ifndef PARTPLAY_APR_CONVERTER_HPP
#define PARTPLAY_APR_CONVERTER_HPP

#include "src/data_structures/Mesh/meshclass.h"
#include "src/data_structures/APR/APR.hpp"

#include "src/algorithm/gradient.hpp"

template<typename ImageType>
class APR_converter {

public:

    APR_converter():image_type("uint16"){

    }

    APR_parameters par;

    std::string image_type; //default uint16

    /*
     * Declerations
     */


    bool get_apr(APR<ImageType>& apr){
        //
        //  Different input image types
        //

        if(image_type == "uint8"){
            return get_apr_method<uint8_t>(apr);
        } else if (image_type == "float"){
            return get_apr_method<float>(apr);
        } else {
            return get_apr_method<uint16_t>(apr);
        }

    };




private:
    template<typename T>
    void init_apr(Mesh_data<T>& input_img);

    template<typename T>
    void auto_parameters(Mesh_data<T>& input_img);

    template<typename T>
    bool get_apr_method(APR<ImageType>& apr);

};

/*
 * Implimentations
 */
template<typename ImageType> template<typename T>
bool APR_converter<ImageType>::get_apr_method(APR<ImageType>& apr) {

    APR_timer timer;
    timer.verbose_flag = true;

    timer.start_timer("read tif input image");

    //input type
    Mesh_data<T> input_image;

    input_image.load_image_tiff(par.input_dir + par.input_image_name);

    timer.stop_timer();

    //    was there an image found
    if(input_image.mesh.size() == 0){
        std::cout << "Image Not Found" << std::endl;
        return false;
    }

    timer.start_timer("calculate automatic parameters");
    auto_parameters(input_image);
    timer.stop_timer();


    return false;
}

template<typename ImageType> template<typename T>
void APR_converter<ImageType>::auto_parameters(Mesh_data<T>& input_img){
    //
    //  Simple automatic parameter selection for 3D APR Flouresence Images
    //


    //minimum element
    T min_val = *std::min_element(input_img.mesh.begin(),input_img.mesh.end());

    // will need to deal with grouped constant or zero sections in the image somewhere.... but lets keep it simple for now.

    std::vector<uint64_t> freq;
    unsigned int num_bins = 10000;
    freq.resize(num_bins);

    uint64_t counter = 0;
    double total=0;

    uint64_t q =0;
//#pragma omp parallel for default(shared) private(q)
    for (q = 0; q < input_img.mesh.size(); ++q) {

        if(input_img.mesh[q] < (min_val + num_bins-1)){
            freq[input_img.mesh[q]-min_val]++;
            if(input_img.mesh[q] > 0) {
                counter++;
                total += input_img.mesh[q];
            }
        }
    }

    float img_mean = total/(counter*1.0);

    float prop_total_th = 0.05; //assume there is atleast 5% background in the image
    float prop_total = 0;

    uint64_t min_j = 0;

    // set to start at one to ignore potential constant regions thresholded out. (Common in some images)
    for (int j = 1; j < num_bins; ++j) {
        prop_total += freq[j]/(counter*1.0);

        if(prop_total > prop_total_th){
            min_j = j;
            break;
        }

    }


    Mesh_data<T> histogram;
    histogram.initialize(num_bins,1,1);

    std::copy(freq.begin(),freq.end(),histogram.mesh.begin());

    float tol = 0.0001;
    float lambda = 3;

    //Y direction bspline

    ///
    /// Smooth the histogram results using Bsplines
    ///
    bspline_filt_rec_y(histogram,lambda,tol);

    calc_inv_bspline_y(histogram);

    ///
    /// Calculate the local maximum after 5%  of the background on the smoothed histogram
    ///

    unsigned int local_max_j = 0;
    uint64_t local_max = 0;

    for (int k = min_j; k < num_bins; ++k) {

        if(histogram.mesh[k] >= ((histogram.mesh[k-1] + histogram.mesh[k-2])/2.0)){
        } else {
            local_max = histogram.mesh[k];
            local_max_j = k;
            break;
        }
    }


    T estimated_first_mode = local_max_j + min_val;

    int stop = 1;

    std::vector<std::vector<T>> patches;

    patches.resize(std::min(local_max,(uint64_t)10000));

    for (int l = 0; l < patches.size(); ++l) {
        patches[l].resize(27,0);
    }


    unsigned int z_num = input_img.z_num;
    unsigned int x_num = input_img.x_num;
    unsigned int y_num = input_img.y_num;

    int j = 0;
    int k = 0;
    int i = 0;

    int j_n = 0;
    int k_n = 0;
    int i_n = 0;

    uint64_t counter_p = 0;

    for(j = 1; j < (z_num-1);j++){
        for(i = 1; i < (x_num-1);i++){
            for(k = 1;k < (y_num-1);k++){

                float val = input_img.mesh[j*x_num*y_num + i*y_num + k];

                if(val == estimated_first_mode) {

                    uint64_t counter_n = 0;

                    for (int l = -1; l < 2; ++l) {
                        for (int m = -1; m < 2; ++m) {
                            for (int n = -1; n < 2; ++n) {
                                patches[counter_p][counter_n] = input_img.mesh[(j+l)*x_num*y_num + (i+m)*y_num + (k+n)];
                                counter_n++;
                            }
                        }
                    }

                    counter_p++;

                }
                if(counter_p > (patches.size()-1)){
                    goto finish;
                }

            }
        }
    }

    finish:

    //first compute the mean over all the patches.

    double total_p=0;
    counter = 0;

    for (int i = 0; i < patches.size(); ++i) {
        for (int j = 0; j < patches[i].size(); ++j) {

            if(patches[i][j] > 0){
                total_p += patches[i][j];
                counter++;
            }
        }
    }

    T mean = total_p/(counter*1.0);

    //now compute the standard deviation (sd) of the patches

    double var=0;

    for (int i = 0; i < patches.size(); ++i) {
        for (int j = 0; j < patches[i].size(); ++j) {

            if(patches[i][j] > 0){
                var += pow(patches[i][j]-mean,2);
            }
        }
    }

    var = var/(counter*1);

    float sd = sqrt(var);

    float min_snr = 6;

    if(this->par.SNR_min > 0){
        min_snr = this->par.SNR_min;
    } else {
        std::cout << "**Assuming a minimum SNR of 6" << std::endl;
    }

    std::cout << "**Assuming image has atleast 5% dark background" << std::endl;

    float Ip_th = mean + sd;

    float var_th = (img_mean/mean)*sd*min_snr;

    float var_th_max = sd*min_snr*.5;

    if(this->par.Ip_th < 0 ){
        this->par.Ip_th = Ip_th;
    }

    if(this->par.lambda < 0){
        this->par.lambda = 3.0;
    }

    if(this->par.min_signal < 0){
        this->par.sigma_th = var_th;
        this->par.sigma_th_max = var_th_max;
    } else{
        this->par.sigma_th_max = this->par.min_signal*0.5;
        this->par.sigma_th = this->par.min_signal;
    }

    std::cout << "I_th: " << this->par.Ip_th << std::endl;
    std::cout << "sigma_th: " << this->par.sigma_th << std::endl;
    std::cout << "sigma_th_max: " << this->par.sigma_th_max << std::endl;
    std::cout << "relative error (E): " << this->par.rel_error << std::endl;
    std::cout << "Lambda: " << this->par.lambda << std::endl;

}


#endif //PARTPLAY_APR_CONVERTER_HPP
