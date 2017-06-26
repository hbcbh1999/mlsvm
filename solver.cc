#include "solver.h"
#include "config_logs.h"
#include "loader.h"
#include <algorithm>    // std::random_shuffle


struct svm_node *x;


//======================================================================
void Solver::free_solver(std::string caller_name){
#if dbl_SV_free_solver >= 1 // 1 default
    std::cout <<"free solver is called by: "<< caller_name << std::endl;
#endif
    svm_free_and_destroy_model(&local_model);
    svm_destroy_param(&param);
    free(prob.y);
    free(prob.x);
    free(x_space);
}





svm_model * Solver::train_model(Mat& m_data_p, Vec& v_vol_p, Mat& m_data_n, Vec& v_vol_n,
                                bool inherit_params, double param_c, double param_gamma){

    ETimer t_sv_tm;
    // - - - - - get dimensions - - - - -
    PetscInt p_num_row_, n_num_row_, num_col_;
//    int total_num_point_ = 0;
    MatGetSize(m_data_p, &p_num_row_, &num_col_);
    MatGetSize(m_data_n, &n_num_row_, NULL);
//    total_num_point_ = p_num_row_ + n_num_row_;

    read_parameters();
    if(inherit_params){
        this->param.C = param_c;
        this->param.gamma = param_gamma;
    }

    int error_cnt=0;

    const char *error_msg = NULL;                                  //check parameters
    do{
        error_msg = svm_check_parameter(&prob,&param);
        if(error_msg){
            error_cnt++;
            fprintf(stderr,"[SV][TM] ERROR: %s\n",error_msg);
            print_parameters();
            if(param.svm_type != 0) {
                param.svm_type = 0;
                param.degree = 3;
            }
        }
        if(error_cnt > 3) exit(1);
    }while(error_msg);                                                       // now prob and param are loaded and checked
//    std::cout << "[SV][TM] after read parameters:"<< "\n";

    read_problem(m_data_p, v_vol_p, m_data_n,v_vol_n);

#if weight_instance == 0    // without instance weight support
    if(Config_params::getInstance()->get_ms_svm_id()==2){                   //Weighted SVM
        alloc_memory_for_weights(param, 0);     // 0 means don't free old memory and it makes sense because this is the first time
//        set_weights_num_points(param, p_num_row_, n_num_row_);
        set_weights_sum_volume(param, v_vol_p, v_vol_n);
    }else{
        param.weight = NULL;
        param.weight_label=NULL;
        param.nr_weight=0;
    }
#else    // - - - - the instance weights are set instead of simple class weights - - - -
    param.weight = NULL;
    param.weight_label=NULL;
    param.nr_weight=0;
#endif

    local_model = svm_train(&prob,&param);

#if dbl_SV_TM >= 1
    std::cout << "[SV][TM] param C:"<< local_model->param.C <<", gamma:" << local_model->param.gamma << std::endl;
#endif

#if dbl_SV_TM_report_time == 1
    t_sv_tm.stop_timer("train model");
#endif
    return local_model;
}




svm_model * Solver::train_model_index_base(Mat& m_data_p, Vec& v_vol_p, Mat& m_data_n, Vec& v_vol_n,
                                           std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                                           PetscInt iter_p_end, PetscInt iter_n_end,
                                           bool inherit_params, double param_c, double param_gamma){

    ETimer t_sv_tm;

    read_parameters();
    if(inherit_params){
        this->param.C = param_c;
        this->param.gamma = param_gamma;
    }else{
        fprintf(stderr,"[SV][TMIB] ERROR: no C, G are passed to train_model_index_base, Exit!\n");
        exit(1);
    }

    const char *error_msg;                                  //check parameters
    error_msg = svm_check_parameter(&prob,&param);
    if(error_msg) {
        fprintf(stderr,"[SV][TMIB] ERROR: %s\n",error_msg);
        print_parameters();
        exit(1);
    }                                                       // now prob and param are loaded and checked

    read_problem_index_base(m_data_p, m_data_n, v_p_index, v_n_index, iter_p_end, iter_n_end,v_vol_p, v_vol_n );

#if weight_instance == 0    // without instance weight support
    if(Config_params::getInstance()->get_ms_svm_id()==2){                   //Weighted SVM
        alloc_memory_for_weights(param, 0);     // 0 means don't free old memory and it makes sense because this is the first time
        set_weights_sum_volume_index_base(param, v_vol_p, v_vol_n, v_p_index, v_n_index, iter_p_end, iter_n_end);
    }else{
        param.weight = NULL;
        param.weight_label=NULL;
        param.nr_weight=0;
    }
#else    // - - - - the instance weights are set instead of simple class weights - - - -
    param.weight = NULL;
    param.weight_label=NULL;
    param.nr_weight=0;
#endif


    local_model = svm_train(&prob,&param);
#if dbl_SV_TM >= 1
    std::cout << "[SV][TMIB] param C:"<< local_model->param.C <<", gamma:" << local_model->param.gamma << std::endl;
#endif
#if dbl_SV_TM_report_time == 1
    t_sv_tm.stop_timer("train model index base");
#endif
    return local_model;
}




void Solver::stand_alone_train_without_instance_weight(Mat& m_data_p, Mat& m_data_n, std::string model_fname){
    ETimer t_sv_sat;
    // - - - - - get dimensions - - - - -
    PetscInt p_num_row_, n_num_row_, num_col_;
    MatGetSize(m_data_p, &p_num_row_, &num_col_);
    MatGetSize(m_data_n, &n_num_row_, NULL);

    read_parameters();
    std::cout << "[SV][SAT] param.svm_type:" << param.svm_type << std::endl;
    std::cout << "[SV][SAT] param.C:" << param.C << std::endl;
    std::cout << "[SV][SAT] param.gamma:" << param.gamma << std::endl;
    std::cout << "[SV][SAT] param.e:" << param.eps << std::endl;
    const char *error_msg;                                  //check parameters
    error_msg = svm_check_parameter(&prob,&param);
    if(error_msg) {
        fprintf(stderr,"[SV][SAT] ERROR: %s\n",error_msg);
        print_parameters();
        exit(1);
    }
    // now prob and param are loaded and checked

//    std::cout << "\n\n[SV][SAT] haven't create another read_problem after I modify it to get the volumes Aug31,2016 , Exit!\n" <<  std::endl;
//    exit(1);
    read_problem_without_instance_weight(m_data_p, m_data_n);

    if(Config_params::getInstance()->get_ms_svm_id()==2){                   //Weighted SVM
        alloc_memory_for_weights(param, 0);     // 0 means don't free old memory and it makes sense because this is the first time
        set_weights_num_points(param, p_num_row_, n_num_row_);
//        param.weight[0]=  0.0102041;
//        param.weight[1]=  0.00249377;
        printf("[SV][SAT] Min class P(label :%d,\t # row:%d,\t weight:%g)\n",param.weight_label[0], p_num_row_, param.weight[0]);
        printf("[SV][SAT] Maj class N(label :%d,\t # row:%d,\t weight:%g)\n",param.weight_label[1], n_num_row_, param.weight[1]);
    }else{
        param.weight = NULL;
        param.weight_label=NULL;
        param.nr_weight=0;
    }

    #if weight_instance == 1
        for (int i=0 ; i < prob.l; i++){
            prob.W[i] = 1;
        }
    #endif

    local_model = svm_train(&prob,&param);
    solution tmp_sol;
    prepare_solution_single_model(local_model, p_num_row_, tmp_sol);

    svm_save_model(model_fname.c_str(), local_model);
    printf("model saved in %s file\n", model_fname.c_str());
    t_sv_sat.stop_timer("stand alone train");
}



void Solver::stand_alone_train_instance_weight(Mat& m_data_p , Vec& v_vol_p, Mat& m_data_n, Vec& v_vol_n, std::string model_fname){
    ETimer t_sv_sat;
    // - - - - - get dimensions - - - - -
    PetscInt p_num_row_, n_num_row_, num_col_;
    MatGetSize(m_data_p, &p_num_row_, &num_col_);
    MatGetSize(m_data_n, &n_num_row_, NULL);

    read_parameters();
    std::cout << "[SV][SAT] param.svm_type:" << param.svm_type << std::endl;
    std::cout << "[SV][SAT] param.C:" << param.C << std::endl;
    std::cout << "[SV][SAT] param.gamma:" << param.gamma << std::endl;
    std::cout << "[SV][SAT] param.e:" << param.eps << std::endl;
    const char *error_msg;                                  //check parameters
    error_msg = svm_check_parameter(&prob,&param);
    if(error_msg) {
        fprintf(stderr,"[SV][SAT] ERROR: %s\n",error_msg);
        print_parameters();
        exit(1);
    }
    // now prob and param are loaded and checked

    read_problem(m_data_p,v_vol_p,m_data_n,v_vol_n);

    // - - - - the instance weights are set instead of simple class weights - - - -
    param.weight = NULL;
    param.weight_label=NULL;
    param.nr_weight=0;



    local_model = svm_train(&prob,&param);
    solution tmp_sol;
    prepare_solution_single_model(local_model, p_num_row_, tmp_sol);

    svm_save_model(model_fname.c_str(), local_model);
    printf("\nmodel saved in %s file\n", model_fname.c_str());
    t_sv_sat.stop_timer("stand alone train");
}


void Solver::PD_train_model_index_base(Mat& m_data, std::vector<int>& v_target_lbl,
                                        const PetscScalar * arr_train_index, PetscInt num_nnz_train,
                                        bool inherit_params, double param_c, double param_gamma){

    ETimer t_sv_PDtmib;

    read_parameters();
    if(inherit_params){
        this->param.C = param_c;
        this->param.gamma = param_gamma;
    }else{
        fprintf(stderr,"[SV][PDTMIB] ERROR: no C, G are passed to PD_train_model_index_base, Exit!\n");
        exit(1);
    }
//    std::cout << "[SV][PDTMIB] before training param C:"<< this->param.C <<", gamma:" << this->param.gamma
//              <<", e:" << this->param.eps  << std::endl;


    const char *error_msg;                                  //check parameters
    error_msg = svm_check_parameter(&prob,&param);
    if(error_msg) {
        fprintf(stderr,"[SV][PDTMIB] ERROR: %s\n",error_msg);
        print_parameters();
        exit(1);
    }                                                       // now prob and param are loaded and checked

    PD_read_problem_index_base(m_data, v_target_lbl, arr_train_index, num_nnz_train);

//    Mat m_p_data, m_n_data;
//    MatCreateSeqAIJ(PETSC_COMM_SELF,num_nnz_train,40,40, PETSC_NULL, &m_p_data);
//    MatCreateSeqAIJ(PETSC_COMM_SELF,num_nnz_train,40,40, PETSC_NULL, &m_n_data);
//    get_separate_matrices(m_data, v_target_lbl, arr_train_index, num_nnz_train, m_p_data, m_n_data);
//    read_problem(m_p_data,m_n_data);


    if(Config_params::getInstance()->get_ms_svm_id()==2){                   //Weighted SVM
        alloc_memory_for_weights(param, 0);     // 0 means don't free old memory and it makes sense because this is the first time
        PD_set_weights_sum_num_point_IB(param, v_target_lbl, arr_train_index, num_nnz_train);
    }else{
        param.weight = NULL;
        param.weight_label=NULL;
        param.nr_weight=0;
    }

    local_model = svm_train(&prob,&param);
    svm_save_model("./debug/single_level.model",local_model);

#if dbl_SV_PDTMIB >= 1
    std::cout << "[SV][PDTMIB] param C:"<< local_model->param.C <<", gamma:" << local_model->param.gamma
              <<", e:" << local_model->param.eps ;
    if(Config_params::getInstance()->get_ms_svm_id()==2)                   //Weighted SVM
        std::cout << ", w1:" << local_model->param.weight[0]
                      <<", w-1:" << local_model->param.weight[1] ;
    std::cout << std::endl;
#endif
#if dbl_SV_TM_report_time == 1
    t_sv_PDtmib.stop_timer("train model index base");
#endif
}





void Solver::partial_solver(Mat& p_data, Vec& v_vol_p, Mat& n_data, Vec& v_vol_n, double last_c, double last_gamma,
                        int level, std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                        std::unordered_set<PetscInt>& uset_SV_index_p, std::unordered_set<PetscInt>& uset_SV_index_n,
                        Mat& m_VD_p, Mat& m_VD_n, Mat& m_VD_both, Mat& m_all_predict_VD, Mat& m_testdata, int classifier_id, Mat& m_all_predict_TD){
    ETimer t_sv_ps;
    // - - - - - - set parameters - - - - - -
    read_parameters();
    param.C = last_c;
    param.gamma = last_gamma;

    const char *error_msg;                                  //check parameters
    error_msg = svm_check_parameter(&prob,&param);
    if(error_msg) {
        printf("[SV][PS]  ERROR: %s\n",error_msg);
        fprintf(stderr,"ERROR: %s\n",error_msg);
        print_parameters();
        exit(1);
    }
    // - - - - - - read the training part - - - - - -
    read_problem_index_base(p_data,n_data,v_p_index, v_n_index, v_p_index.size(), v_n_index.size(), v_vol_p, v_vol_n);

//    std::cout<< "[SV][PS] read_problem_index_base finished successfully! " <<std::endl;

    PetscInt p_num_row_ = v_p_index.size();
    PetscInt n_num_row_ = v_n_index.size();
    // - - - - - - - set weights only for Weighted SVM - - - - - - - -
    if(Config_params::getInstance()->get_ms_svm_id()==2){
        alloc_memory_for_weights(param, 0);     // 0 means don't free old memory and it makes sense because this is the first time

        if(Config_params::getInstance()->get_rf_weight_vol()){      //based on volume
            //calc sum of volumes       //TODO
            set_weights_sum_volume_index_base(param, v_vol_p, v_vol_n, v_p_index, v_n_index, v_p_index.size(), v_n_index.size());
        }else{                                                      //based on number of points
            set_weights_num_points(param, p_num_row_, n_num_row_);
        }
    }else{
        param.weight = NULL;
        param.weight_label=NULL;
        param.nr_weight=0;
    }

    local_model = svm_train(&prob,&param);
//    t_sv_ps.stop_timer("[SV][PS] model training");

    /// - - - - - - - - predict the validation data - - - - - - - - -
    // this is used to check the quality of this classifier on the test data in the end,
    // as this method is called in the refinement with partitioning, the predicted labels
    // for validation data is stores in a related column of a prediction matrix to a group of partitions
    predict_test_data_in_matrix_output(m_testdata, classifier_id, m_all_predict_TD);
    predict_test_data_in_matrix_output(m_VD_both, classifier_id, m_all_predict_VD);     //added 021517-1920


/// - - - - - - - - prepare the solution for refinement - - - - - - - - -

    if(level > 1 ){     // at the finest level, we need to save the model (SV, C, gamma) for unseen points
//        ETimer t_solution;
        // ----- create the index of SVs in data points for each class seperately ----
        PetscInt i;
        for (i=0; i < local_model->nSV[0];i++){
            // -1 because sv_indice start from 1, while petsc row start from 0
            // the original index in data matrix is saved in v_p_index
            uset_SV_index_p.insert(v_p_index[local_model->sv_indices[i] - 1]);
        }

        // add the index in the model for it after subtract from number of minority in training data
        for (int i=0; i < local_model->nSV[1];i++){
            // -1 the same as pos class, p_num_row because they are after each other
            // the original index in data matrix is saved in v_n_index
            uset_SV_index_n.insert(v_n_index[ local_model->sv_indices[local_model->nSV[0] + i] - 1 - p_num_row_]);
        }

#if dbl_MS_PS >= 3
        printf("[SV][PS] partial solver has nSV+:%d, nSV-:%d\n", local_model->nSV[0], local_model->nSV[1]);
#endif
//        t_solution.stop_timer("[SV][PS] prepare the partial solution (except finest level) at level:",std::to_string(level));
    }else{
        if(Config_params::getInstance()->get_ms_save_final_model()){
            printf("[SV][PS] at the finest level the partial models needs to be saved!!! Exit \n");
//             exit(1);
            //            svm_save_model("./final_model.svm",model);
        }
        //TODO: multiple hyperplain
    }

}




//void Solver::cross_fold_data(const Mat data_p, const Mat data_n,
//                                        Mat& train_p_, Mat& train_n_, Mat& test_total_){
////divide data to train and test randomely
//    Mat             test_p_, test_n_;
//    IS              isrow_train_p_, isrow_train_n_;
//    IS              isrow_test_p_, isrow_test_n_;
//    PetscInt        * ind_train_p_, * ind_train_n_;                 //arrays of Int that contains the row indices (Training)
//    PetscInt        * ind_test_p_, * ind_test_n_;                 //arrays of Int that contains the row indices (Test)
//    PetscInt        size_total_p_=0, size_total_n_=0;
//    PetscInt        num_col_p_=0, num_col_n_=0, max_num_col=0;
//    PetscInt        size_train_p_=0, size_train_n_=0;
//    PetscInt        size_test_p_=0, size_test_n_=0;       //test = total - train

//    MatGetSize(data_p,&size_total_p_,&num_col_p_);                  //get the size of data in positive (minority) class
//    MatGetSize(data_n,&size_total_n_,&num_col_n_);

//    // training part is rest of validation
//    double train_part_percentage =  1 - Config_params::getInstance()->get_ms_validation_part();
//    size_train_p_ = floor(size_total_p_ * train_part_percentage) ;
//    size_train_n_ = floor(size_total_n_ * train_part_percentage) ;

//    size_test_p_ = size_total_p_ - size_train_p_;
//    size_test_n_ = size_total_n_ - size_train_n_;
//#if dbl_MS_cross_fold >= 1
//    printf("[SV][cross_fold]  tr_p :%d, tr_n :%d, number of columns:%d\n",size_train_p_,size_train_n_,num_col_p_);
//#endif

//    PetscMalloc1(size_train_p_, &ind_train_p_);
//    PetscMalloc1(size_test_p_, &ind_test_p_);

//    PetscMalloc1(size_train_n_, &ind_train_n_);
//    PetscMalloc1(size_test_n_, &ind_test_n_);
//// Random generator without duplicates
//                                    //---- Positive class training ----
//    std::vector<int> vec_p_indices;
//    vec_p_indices.reserve(size_total_p_);
//    for (int i_p=0; i_p<size_total_p_; ++i_p){       //create a vector of all possible nodes
//        vec_p_indices.push_back(i_p);
//    }

//    srand(std::stoll(Config_params::getInstance()->get_cpp_srand_seed()));
//    std::random_shuffle ( vec_p_indices.begin(), vec_p_indices.end() ); //shuffle all nodes

//    for (int i =0;i != size_train_p_ ; i++){        // only pick the train size number of them
//        ind_train_p_[i] = vec_p_indices[i];
//    }

//    // ind_train_p should sort
//    std::sort(ind_train_p_,ind_train_p_ + size_train_p_);   //this is critical for MatGetSubMatrix method

//                                    ///---- Positive class test ----
//    for (int i =0;i != size_test_p_; i++){        // only pick the rest of nodes (after training to the end)
//        ind_test_p_[i] = vec_p_indices[i+size_train_p_];
//    }

//    // ind_test_p should sort
//    std::sort(ind_test_p_,ind_test_p_ + size_test_p_);       // this is critical for MatGetSubMatrix method

//                                    //---- Negative class  training ----
//    std::vector<int> vec_n_indices;
//    vec_n_indices.reserve(size_total_n_);
//    for (int i_n=0; i_n <size_total_n_; ++i_n)
//        vec_n_indices.push_back(i_n);
//    srand(std::stoll(Config_params::getInstance()->get_cpp_srand_seed()));
//    std::random_shuffle ( vec_n_indices.begin(), vec_n_indices.end() );


//    for (int i =0;i != size_train_n_ ; i++){
//        ind_train_n_[i] = vec_n_indices[i];
//    }

//    // ind_train_n should sort
//    std::sort(ind_train_n_,ind_train_n_ + size_train_n_);

//                                    ///---- Negative class test ----
//    for (int i =0;i != size_test_n_; i++){        // only pick the rest of nodes (after training to the end)
//    ind_test_n_[i] = vec_n_indices[i+size_train_n_];
//    }

//    // ind_test_n should sort
//    std::sort(ind_test_n_,ind_test_n_ + size_test_n_);       // this is critical for MatGetSubMatrix method

//    ISCreateGeneral(PETSC_COMM_SELF,size_train_p_,ind_train_p_,PETSC_COPY_VALUES,&isrow_train_p_);
//    ISCreateGeneral(PETSC_COMM_SELF,size_test_p_,ind_test_p_,PETSC_COPY_VALUES,&isrow_test_p_);

//    ISCreateGeneral(PETSC_COMM_SELF,size_train_n_,ind_train_n_,PETSC_COPY_VALUES,&isrow_train_n_);
//    ISCreateGeneral(PETSC_COMM_SELF,size_test_n_,ind_test_n_,PETSC_COPY_VALUES,&isrow_test_n_);
////    ISView(isrow_train_p_,PETSC_VIEWER_STDOUT_SELF);

//    PetscFree(ind_train_p_);
//    PetscFree(ind_test_p_);

//    PetscFree(ind_train_n_);
//    PetscFree(ind_test_n_);

//    MatGetSubMatrix(data_p,isrow_train_p_, NULL,MAT_INITIAL_MATRIX,&train_p_);
//    MatGetSubMatrix(data_p,isrow_test_p_, NULL,MAT_INITIAL_MATRIX,&test_p_);

//    MatGetSubMatrix(data_n,isrow_train_n_, NULL,MAT_INITIAL_MATRIX,&train_n_);
//    MatGetSubMatrix(data_n,isrow_test_n_, NULL,MAT_INITIAL_MATRIX,&test_n_);

//    ISDestroy(&isrow_train_p_);
//    ISDestroy(&isrow_test_p_);

//    ISDestroy(&isrow_train_n_);
//    ISDestroy(&isrow_test_n_);

//    if(num_col_p_ >= num_col_n_)
//        max_num_col = num_col_p_;
//    else
//        max_num_col = num_col_n_;
//#if dbl_MS_cross_fold >= 1
//    printf("[SV][cross_fold] max number of columns :%d\n",max_num_col);
//#endif

//    MatGetSize(train_p_,&size_total_p_,NULL);                  //get the size of data in positive (minority) class
//    MatGetSize(test_p_,&size_test_p_,NULL);                  //get the size of data in positive (minority) class

//    MatGetSize(train_n_,&size_total_n_,NULL);
//    MatGetSize(test_n_,&size_test_n_,NULL);
//#if dbl_MS_cross_fold >= 1
//    printf("[SV][cross_fold]train_p size :%d\n",size_train_p_);                                               //$$debug
//    printf("[SV][cross_fold]test_p size :%d\n",size_test_p_);                                               //$$debug

//    printf("[SV][cross_fold]train_n size :%d\n",size_train_n_);                                               //$$debug
//    printf("[SV][cross_fold]test_n size :%d\n",size_test_n_);                                               //$$debug
//#endif
//#if dbl_MS_cross_fold >= 7
//    printf("[SV][cross_fold]train_p Matrix:\n");                                               //$$debug
//    MatView(train_p_,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug

//    printf("[SV][cross_fold]train_n Matrix:\n");                                               //$$debug
//    MatView(train_n_,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
//#endif

//    /// Mix both test matrices to one matrix
//    combine_test_data(test_total_,test_p_,size_test_p_,test_n_,size_test_n_,max_num_col);


//}






//=================== combine_test_data ==============================
// Get 2 matrices and make a new one while destroy both initial matrices
void Solver::combine_test_data(Mat& test_total, Mat& dt_test_p,
                                       PetscInt size_p, Mat& dt_test_n, PetscInt size_n, PetscInt max_num_col_){
    PetscInt num_row = size_p + size_n;
    PetscInt i, ncols;
    const PetscInt    *cols;
    const PetscScalar *vals;
//    PetscInt total_curr_row = 0;
//    MatCreateSeqAIJ(PETSC_COMM_SELF,num_row ,max_num_col_ + 1 ,pre_init_loader_matrix,PETSC_NULL, &test_total); //+1 is for label

    MatCreateSeqAIJ(PETSC_COMM_SELF,num_row ,max_num_col_ + 1 ,(max_num_col_ + 1 ),PETSC_NULL, &test_total); //+1 is for label
    for(i =0; i < size_p ; i++){
        MatSetValue(test_total, i, 0, +1,INSERT_VALUES);        //Insert positive label
        MatGetRow(dt_test_p,i,&ncols,&cols,&vals);
        for(int j=0; j < ncols ; j++){
            MatSetValue(test_total,i,cols[j]+1, vals[j],INSERT_VALUES) ;    //+1 shifts the columns 1 to the right
        }
        MatRestoreRow(dt_test_p,i,&ncols,&cols,&vals);
    }

    for(i =0; i < size_n ; i++){
        MatSetValue(test_total, i + size_p, 0, -1, INSERT_VALUES);        //Insert negative label
        MatGetRow(dt_test_n,i,&ncols,&cols,&vals);
        for(int j=0; j < ncols ; j++){
            MatSetValue(test_total, i + size_p, cols[j]+1, vals[j],INSERT_VALUES) ;    //+1 shifts the columns 1 to the right
        }
        MatRestoreRow(dt_test_n,i,&ncols,&cols,&vals);
    }
    MatAssemblyBegin(test_total, MAT_FINAL_ASSEMBLY);
    MatAssemblyEnd(test_total, MAT_FINAL_ASSEMBLY);
    MatDestroy(&dt_test_p);                             //release the separated class of test data
    MatDestroy(&dt_test_n);

#if dbl_MS_combine_test_data > 7
    PetscViewer     viewer_testdata;
    PetscViewerBinaryOpen(PETSC_COMM_WORLD,"Coarsest_test_data.dat",FILE_MODE_WRITE,&viewer_testdata);
    MatView(test_total,viewer_testdata);
    PetscViewerDestroy(&viewer_testdata);

    printf("[combine_test_data]total test Matrix:\n");                                               //$$debug
    MatView(test_total ,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
#endif

}







//=========== read the training data using the vector of indices ============
void Solver::read_problem_index_base(Mat& m_train_data_p, Mat& m_train_data_n,
                                        std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                                        PetscInt iter_p_end,PetscInt iter_n_end,
                                        Vec& v_vol_p, Vec& v_vol_n){
#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] DEBUG start Solver::read_problem_index_base\n");
#endif
    PetscInt i=0, j=0, l=0, k=0, ncols;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;
    PetscInt num_col=0, num_total_nodes=0;  //num_row=0,
    PetscInt p_num_node_=0, n_num_node_=0, num_elements_=0;

    PetscScalar sum_all_vol_p=0, sum_all_vol_n=0;       //calculate the sum of all the volumes in each class of the current training set
//    VecSum(v_vol_p, &sum_all_vol_p);
//    VecSum(v_vol_n, &sum_all_vol_n);      // The sum of indices which are sent are important not all the points in the vector
    PetscScalar     *arr_vol_p, *arr_vol_n;
    PetscInt        max_num_p, max_num_n;
    VecGetSize(v_vol_p, &max_num_p);
    VecGetSize(v_vol_p, &max_num_p);

    PetscMalloc1(max_num_p, &arr_vol_p);
    PetscMalloc1(max_num_n, &arr_vol_n);

    VecGetArray(v_vol_p,&arr_vol_p);                // the restore goes at the end of each class
    VecGetArray(v_vol_n,&arr_vol_n);

#if dbl_SV_RPIB >= 7
    printf("[SV][RPIB] m_train_data_p matrix:\n");                   //$$debug
    MatView(m_train_data_p, PETSC_VIEWER_STDOUT_WORLD);
    printf("[SV][RPIB] m_train_data_n matrix:\n");                   //$$debug
    MatView(m_train_data_n, PETSC_VIEWER_STDOUT_WORLD);
#endif


// - - - - - - find number of nodes and elements - - - - - - -
    MatGetSize(m_train_data_p,NULL,&num_col);    // we need the number of columns
    p_num_node_ = iter_p_end;
    n_num_node_ = iter_n_end;

    num_total_nodes = p_num_node_ + n_num_node_;
#if dbl_SV_RPIB >= 1
    printf("[SV][RPIB] number of P_data: %d, N_data: %d, total_nodes :%d \n",
                               p_num_node_,n_num_node_,num_total_nodes);     //$$debug
#endif
//---- Count number of non zero elements -----
    // for positive class
    for (i=0; i< p_num_node_;i++){
        MatGetRow(m_train_data_p, v_p_index[i],&ncols, &cols, &vals);                 // data points
        num_elements_ += ncols + 1;     // +1 : for the end of line index
        MatRestoreRow(m_train_data_p, v_p_index[i],&ncols, &cols, &vals);
        sum_all_vol_p += arr_vol_p[v_p_index[i]];                                  // sum up the selected volumes
    }
#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] number of p elems:%d\n",num_elements_);
#endif
    // for negative class
    for (i=0; i< n_num_node_;i++){
        MatGetRow(m_train_data_n, v_n_index[i],&ncols, &cols, &vals);
        num_elements_ += ncols + 1;     // +1 : for the end of line index
        MatRestoreRow(m_train_data_n, v_n_index[i],&ncols, &cols, &vals);
        sum_all_vol_n += arr_vol_n[v_n_index[i]];                                   // sum up the selected volumes
    }
#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] sum all vol p:%g \t sum all vol n:%g\n",sum_all_vol_p,sum_all_vol_n);                  //$$debug
    #if dbl_SV_RPIB >= 5
        printf("[SV][RPIB] total number of elems for both classes (one column as terminator [-1,0]):%d\n",num_elements_);
    #endif
#endif

#if weight_instance == 1
    PetscScalar        min_vol=0, max_vol=0;
    PetscScalar        sq_inv_sum_vol_p=pow(1.0/sum_all_vol_p, 2);
    PetscScalar        sq_inv_sum_vol_n=pow(1.0/sum_all_vol_n, 2);
//        printf("[SV][RP] sq_inv_sum_vol_p:%.12f, sq_inv_sum_vol_n:%.12f\n",sq_inv_sum_vol_p,sq_inv_sum_vol_n);         //$$debug
#endif



//---- read the data to prob for libsvm -----
    prob.y = Malloc(double, num_total_nodes );

    #if weight_instance == 1
        prob.W = Malloc(double, num_total_nodes );
    #endif

    prob.x = Malloc(struct svm_node *, num_total_nodes );
    x_space = Malloc(struct svm_node, num_elements_ );

#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] After Malloc svm objects\n");
#endif

    // - - - - set the problem from the data and volume - - - -
    prob.l = num_total_nodes;
    // - - - - - read positive data - - - - -
    j=0;                //set the j as an index to go through the x_space
    for (i=0; i< p_num_node_;i++){
        unsigned long ul_target_index = v_p_index[i];
        prob.y[i] = 1;

        #if weight_instance == 1
            prob.W[i] = arr_vol_p[ul_target_index] * sq_inv_sum_vol_p;      ///* instance weight */
            if(prob.W[i] < min_vol)
                min_vol = prob.W[i];    //store the min
            if(prob.W[i] > max_vol)
                max_vol = prob.W[i];    //store the max
//            printf("(%.4f,%.4f),",arr_vol_p[ul_target_index],prob.W[i]);                  //$$debug
        #endif

        prob.x[i] = &x_space[j];
        MatGetRow(m_train_data_p, ul_target_index,&ncols, &cols, &vals);
        l=0;
#if dbl_SV_RPIB >= 3
        if(ncols == 0){
            printf("[SV][RPIB]  *** Error *** Empty row at %d row in m_train_data_p! Exit\n",i);
            exit(1);
        }
#endif
        for (k=0; k< num_col; k++) {    //note this is num_col instead of ncols because of j increament
            if(k == cols[l])
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
            }
#if dbl_SV_RPIB >= 7            //default is 7
            printf("[SV][RPIB] {P} i:%d j:%d prob.y[i]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i], x_space[j].index, x_space[j].value);     //$$debug
            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
                   k, l, cols[l], vals[l]);
#endif
            ++j;
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_p, ul_target_index,&ncols, &cols, &vals);
    }
    VecRestoreArray(v_vol_p,&arr_vol_p);
#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] end of positive class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif

    // - - - - - read negative data - - - - -
    for (i=0; i< n_num_node_;i++){
        unsigned long ul_target_index = v_n_index[i];
        prob.y[i+p_num_node_] = -1;

        #if weight_instance == 1
            prob.W[i+p_num_node_] = arr_vol_n[ul_target_index] * sq_inv_sum_vol_n;
            if(prob.W[i+p_num_node_] < min_vol)
                min_vol = prob.W[i+p_num_node_];    //store the min
            if(prob.W[i+p_num_node_] > max_vol)
                max_vol = prob.W[i+p_num_node_];    //store the max
//            printf("(%.4f,%.4f),",arr_vol_n[ul_target_index], prob.W[i+p_num_node_]);                  //$$debug
        #endif

        prob.x[i+p_num_node_] = &x_space[j];
        MatGetRow(m_train_data_n, ul_target_index,&ncols, &cols, &vals);

        l=0;
        for (k=0; k< num_col; k++) {
            if(k == cols[l])
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
            }
#if dbl_SV_RPIB >= 7
            printf("[SV][RPIB] {N} i:%d j:%d prob.y[i+p_num_node_]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i+p_num_node_], x_space[j].index, x_space[j].value);     //$$debug
//            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
//                   k, l, cols[l], vals[l]);
#endif
            ++j;
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_n, ul_target_index,&ncols, &cols, &vals);
    }
    VecRestoreArray(v_vol_n,&arr_vol_n);
#if dbl_SV_RPIB >= 3
    printf("[SV][RPIB] end of negative class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif

#if weight_instance == 1
    // - - - - normaliz instance weights between 0 and 1
    PetscScalar vol_range = max_vol - min_vol;
//    printf("\n\n[SV][RP] vol range is:%.20f, min vol:%.20f, max vol:%.20f\n",vol_range, min_vol, max_vol);            //$$debug
    for(unsigned int i = 0; i < num_total_nodes; i++){
        prob.W[i] = (prob.W[i] - min_vol) / vol_range ;
//        printf("%d,%.8f\t",i, prob.W[i]);                              //$$debug
    }
#endif
//    Don't Destroy input matrices at all    ( They are deleted after 2nd stage of model selection )
}
















//=========== read the training data using the vector of indices for Personalized classification ============
void Solver::PD_read_problem_index_base(Mat& m_data, std::vector<int>& v_target_lbl, const PetscScalar * arr_index, int num_nnz){
    PetscInt i=0, j=0, l=0, k=0, ncols;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;
    PetscInt num_col=0, num_total_nodes=0;  //num_row=0,
    PetscInt num_elements_=0;
#if dbl_SV_PDRPIB >= 7
    printf("[SV][PDRPIB] p_train_data matrix:\n");                   //$$debug
    MatView(p_train_data, PETSC_VIEWER_STDOUT_WORLD);
#endif

    MatGetSize(m_data,NULL,&num_col);    // we need the number of columns
    num_total_nodes = num_nnz;
//---- Count number of non zero elements -----
    for (i=0; i< num_total_nodes;i++){
        MatGetRow(m_data, arr_index[i],&ncols, &cols, &vals);
        num_elements_ += ncols + 1;     // +1 : for the end of line index
        MatRestoreRow(m_data, arr_index[i],&ncols, &cols, &vals);
    }
#if dbl_SV_PDRPIB >= 3
    printf("[SV][PDRPIB] number of elems:%d\n",num_elements_);
#endif

//---- read the data to prob for libsvm -----
    prob.y = Malloc(double, num_total_nodes );
    prob.x = Malloc(struct svm_node *, num_total_nodes );
    x_space = Malloc(struct svm_node, num_elements_ );

    prob.l = num_total_nodes;
    // - - - - - read data - - - - -
    j=0;                // set the j as an index to go through the x_space
    for (i=0; i< num_total_nodes;i++){
        int idx_data = arr_index[i];        // the index to real data matrix and labels

        prob.y[i] = v_target_lbl[idx_data];            // this is the label
        prob.x[i] = &x_space[j];
        MatGetRow(m_data, idx_data,&ncols, &cols, &vals);
        l=0;
#if dbl_SV_PDRPIB >= 3
        if(ncols == 0){
            printf("[SV][PDRPIB]  *** Error *** Empty row at %d row in m_data! Exit\n",i);
            exit(1);
        }
#endif
        for (k=0; k< num_col; k++) {    //note this is num_col instead of ncols because of j increament
            if(k == cols[l])
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
            }
            ++j;
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_data, idx_data,&ncols, &cols, &vals);
    }

}


















//=========== read the training data (both of the matricex are training data not include test data)============
//void Solver::read_problem(Mat& m_train_data_p, Mat& m_train_data_n){
void Solver::read_problem(Mat& m_train_data_p, Vec& v_vol_p, Mat& m_train_data_n, Vec& v_vol_n){

    PetscInt i=0, j=0, l=0, k=0, ncols;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;
    PetscInt num_col=0, num_total_nodes=0;  //num_row=0,
    PetscInt p_num_node_=0, n_num_node_=0, num_elements_=0;

    PetscScalar sum_all_vol_p=0, sum_all_vol_n=0;       //calculate the sum of all the volumes in each class of the current training set
    VecSum(v_vol_p, &sum_all_vol_p);
    VecSum(v_vol_n, &sum_all_vol_n);
//    printf("\n[SV][RP] Debug weight instance sum_p:%g, sum_n:%g\n\n", sum_all_vol_p, sum_all_vol_n);
#if dbl_SV_read_problem >= 7
    printf("[SV][RP] m_train_data_p matrix:\n");                   //$$debug
    MatView(m_train_data_p, PETSC_VIEWER_STDOUT_WORLD);
    printf("[SV][RP] m_train_data_n matrix:\n");                   //$$debug
    MatView(m_train_data_n, PETSC_VIEWER_STDOUT_WORLD);
#endif

/// find number of nodes and elements
    MatGetSize(m_train_data_p,&p_num_node_,&num_col);    //m returns the number of rows globally
    MatGetSize(m_train_data_n,&n_num_node_,NULL);    //m returns the number of rows globally
    num_total_nodes = p_num_node_ + n_num_node_;
#if dbl_SV_read_problem >= 1
    printf("[SV][RP] number of P_data: %d, N_data: %d, total_nodes :%d \n",
                               p_num_node_,n_num_node_,num_total_nodes);     //$$debug
#endif
//---- Count number of non zero elements -----
    // for positive class
    for (i=0; i< p_num_node_;i++){
        MatGetRow(m_train_data_p,i,&ncols, &cols, &vals);
        num_elements_ += ncols + 1;
        MatRestoreRow(m_train_data_p,i,&ncols, &cols, &vals);
    }
#if dbl_SV_read_problem >= 3
    printf("[SV][RP] number of p elems:%d\n",num_elements_);
#endif
    // for negative class
    for (i=0; i< n_num_node_;i++){
        MatGetRow(m_train_data_n,i,&ncols, &cols, &vals);
        num_elements_ += ncols + 1;
        MatRestoreRow(m_train_data_n,i,&ncols, &cols, &vals);
    }
#if dbl_SV_read_problem >= 5
    printf("[SV][RP]{correct _ NNZ} number of total elems(one column as terminator [-1,0]):%d\n",num_elements_);
#endif

//---- read the data to prob for libsvm -----
    this->prob.y = Malloc(double, num_total_nodes );

    #if weight_instance == 1
        this->prob.W = Malloc(double, num_total_nodes );
    #endif

    this->prob.x = Malloc(struct svm_node *, num_total_nodes );
    this->x_space = Malloc(struct svm_node, num_elements_ );

#if dbl_SV_read_problem >= 3
    printf("[SV][RP]After Malloc\n");
#endif

    #if weight_instance == 1
        //prepare the vector of volumes
        PetscScalar        * arr_vol_p, * arr_vol_n;
        PetscMalloc1(p_num_node_, &arr_vol_p);
        PetscMalloc1(n_num_node_, &arr_vol_n);
        PetscScalar        min_vol=0, max_vol=0;
        PetscScalar        sq_inv_sum_vol_p=pow(1.0/sum_all_vol_p, 2);
        PetscScalar        sq_inv_sum_vol_n=pow(1.0/sum_all_vol_n, 2);
//        printf("[SV][RP] sq_inv_sum_vol_p:%.12f, sq_inv_sum_vol_n:%.12f\n",sq_inv_sum_vol_p,sq_inv_sum_vol_n);         //$$debug
    #endif



    // - - - - set the problem from the data and volume - - - -
    prob.l = num_total_nodes;
    //read positive data
    #if weight_instance == 1
        VecGetArray(v_vol_p,&arr_vol_p);
    #endif
    j=0;                //set the j as an index to go through the x_space
    for (i=0; i< p_num_node_;i++){
        prob.y[i] = 1;

        #if weight_instance == 1
            prob.W[i] = arr_vol_p[i] * sq_inv_sum_vol_p;    // (vol_x / sum_all_vol_in_same_class) * (1 / sum_all_vol_in_same_class)
                                                            // the left part is importance inside class and the right is between min and maj classes
//            printf("wi:%g\n",arr_vol_p[i]);                  //$$debug
//            printf("%d,%.8f\t, ",i,prob.W[i]);                  //$$debug
            if(prob.W[i] < min_vol)
                min_vol = prob.W[i];    //store the min
            if(prob.W[i] > max_vol)
                max_vol = prob.W[i];    //store the max
        #endif

        prob.x[i] = &x_space[j];
        MatGetRow(m_train_data_p,i,&ncols, &cols, &vals);
        l=0;
#if dbl_SV_read_problem >= 3
        if(ncols == 0){
            printf("[SV][RP] *** Error *** Empty row at %d row in m_train_data_p\n",i);
            exit(1);
        }
#endif
        for (k=0; k< num_col; k++) {    //note this is num_col instead of ncols because of j increament
            if(k == cols[l] &&  fabs(vals[l]) > 0.000001)
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
#if dbl_SV_read_problem >= 7            //default is 7
            printf("[SV][RP]{P} i:%d j:%d prob.y[i]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i], x_space[j].index, x_space[j].value);     //$$debug
//            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
//                   k, l, cols[l], vals[l]);
#endif
                ++j;

            }
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_p,i,&ncols, &cols, &vals);
    }
    #if weight_instance == 1
        VecRestoreArray(v_vol_p,&arr_vol_p);
    #endif
#if dbl_SV_read_problem >= 3 //default is 3
    printf("\n[SV][RP] end of positive class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif
//    printf("\n\n[SV][RP] Debug weight instance");
    //read negative data
    #if weight_instance == 1
        VecGetArray(v_vol_n,&arr_vol_n);
    #endif
    for (i=0; i< n_num_node_;i++){
        prob.y[i+p_num_node_] = -1;

        #if weight_instance == 1
            prob.W[i+p_num_node_] = arr_vol_n[i] * sq_inv_sum_vol_n;
//            printf("%d,%.8f\t",i+p_num_node_, prob.W[i+p_num_node_]);                              //$$debug
            if(prob.W[i+p_num_node_] < min_vol)
                min_vol = prob.W[i+p_num_node_];    //store the min
            if(prob.W[i+p_num_node_] > max_vol)
                max_vol = prob.W[i+p_num_node_];    //store the max
        #endif

        prob.x[i+p_num_node_] = &x_space[j];
        MatGetRow(m_train_data_n,i,&ncols, &cols, &vals);

        l=0;
//        for (k=0; k< ncols; k++) {
        for (k=0; k< num_col; k++) {
            if(k == cols[l] &&  fabs(vals[l]) > 0.000001)
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
#if dbl_SV_read_problem >= 7
            printf("[SV][RP]{N} i:%d j:%d prob.y[i+p_num_node_]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i+p_num_node_], x_space[j].index, x_space[j].value);     //$$debug
//            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
//                   k, l, cols[l], vals[l]);
#endif
                ++j;

            }
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_n,i,&ncols, &cols, &vals);
    }
    #if weight_instance == 1
        VecRestoreArray(v_vol_n,&arr_vol_n);
    #endif
#if dbl_SV_read_problem >= 5
    printf("[SV][RP] end of negative class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif

#if weight_instance == 1
    // - - - - normaliz instance weights between 0 and 1
    PetscScalar vol_range = max_vol - min_vol;
//    printf("\n\n[SV][RP] vol range is:%.20f, min vol:%.20f, max vol:%.20f\n",vol_range, min_vol, max_vol);            //$$debug
    for(unsigned int i = 0; i < num_total_nodes; i++){
        prob.W[i] = (prob.W[i] - min_vol) / vol_range ;
//        printf("%d,%.8f\t",i, prob.W[i]);                              //$$debug
    }
#endif

//    Destroy p_data, n_data    ( They are deleted after 2nd stage of model selection )
//    exit(1);
}


void Solver::read_problem_without_instance_weight(Mat& m_train_data_p, Mat& m_train_data_n){

    PetscInt i=0, j=0, l=0, k=0, ncols;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;
    PetscInt num_col=0, num_total_nodes=0;  //num_row=0,
    PetscInt p_num_node_=0, n_num_node_=0, num_elements_=0;

#if dbl_SV_read_problem >= 7
    printf("[SV][RP] m_train_data_p matrix:\n");                   //$$debug
    MatView(m_train_data_p, PETSC_VIEWER_STDOUT_WORLD);
    printf("[SV][RP] m_train_data_n matrix:\n");                   //$$debug
    MatView(m_train_data_n, PETSC_VIEWER_STDOUT_WORLD);
#endif

/// find number of nodes and elements
    MatGetSize(m_train_data_p,&p_num_node_,&num_col);    //m returns the number of rows globally
    MatGetSize(m_train_data_n,&n_num_node_,NULL);    //m returns the number of rows globally
    num_total_nodes = p_num_node_ + n_num_node_;
#if dbl_SV_read_problem >= 1
    printf("[SV][RP] number of P_data: %d, N_data: %d, total_nodes :%d \n",
                               p_num_node_,n_num_node_,num_total_nodes);     //$$debug
#endif
//---- Count number of non zero elements -----
    // for positive class
    for (i=0; i< p_num_node_;i++){
        MatGetRow(m_train_data_p,i,&ncols, &cols, &vals);
        num_elements_ += ncols + 1;
        MatRestoreRow(m_train_data_p,i,&ncols, &cols, &vals);
    }
#if dbl_SV_read_problem >= 3
    printf("[SV][RP] number of p elems:%d\n",num_elements_);
#endif
    // for negative class
    for (i=0; i< n_num_node_;i++){
        MatGetRow(m_train_data_n,i,&ncols, &cols, &vals);
        num_elements_ += ncols + 1;
        MatRestoreRow(m_train_data_n,i,&ncols, &cols, &vals);
    }
#if dbl_SV_read_problem >= 5
    printf("[SV][RP]{correct _ NNZ} number of total elems(one column as terminator [-1,0]):%d\n",num_elements_);
#endif

//---- read the data to prob for libsvm -----
    this->prob.y = Malloc(double, num_total_nodes );
    this->prob.x = Malloc(struct svm_node *, num_total_nodes );
    this->x_space = Malloc(struct svm_node, num_elements_ );

#if dbl_SV_read_problem >= 3
    printf("[SV][RP]After Malloc\n");
#endif



    // - - - - set the problem from the data and volume - - - -
    prob.l = num_total_nodes;
    //read positive data
    j=0;                //set the j as an index to go through the x_space
    for (i=0; i< p_num_node_;i++){
        prob.y[i] = 1;

        prob.x[i] = &x_space[j];
        MatGetRow(m_train_data_p,i,&ncols, &cols, &vals);
        l=0;
#if dbl_SV_read_problem >= 3
        if(ncols == 0){
            printf("[SV][RP] *** Error *** Empty row at %d row in m_train_data_p\n",i);
            exit(1);
        }
#endif
        for (k=0; k< num_col; k++) {    //note this is num_col instead of ncols because of j increament
            if(k == cols[l])
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
            }
#if dbl_SV_read_problem >= 7            //default is 7
            printf("[SV][RP]{P} i:%d j:%d prob.y[i]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i], x_space[j].index, x_space[j].value);     //$$debug
//            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
//                   k, l, cols[l], vals[l]);
#endif
            ++j;
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_p,i,&ncols, &cols, &vals);
    }

#if dbl_SV_read_problem >= 0 //default is 3
    printf("\n[SV][RP] end of positive class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif
    //read negative data
    for (i=0; i< n_num_node_;i++){
        prob.y[i+p_num_node_] = -1;
        prob.x[i+p_num_node_] = &x_space[j];
        MatGetRow(m_train_data_n,i,&ncols, &cols, &vals);

        l=0;
        for (k=0; k< num_col; k++) {
            if(k == cols[l])
            {
                x_space[j].index = k+1;   //the libsvm use 1 index instead of zero
                x_space[j].value = vals[l];
                l++;
            }
#if dbl_SV_read_problem >= 7
            printf("[SV][RP]{N} i:%d j:%d prob.y[i+p_num_node_]:%g x_space[j].index:%d x_space[j].value:%g\n",
                                        i, j, prob.y[i+p_num_node_], x_space[j].index, x_space[j].value);     //$$debug
//            printf("k:%d ,l:%d, cols[l]:%d, vals[l]:%g\n",
//                   k, l, cols[l], vals[l]);
#endif
            ++j;
        }
        //create the end element of each node (-1,0)
        x_space[j].index = -1;
        x_space[j].value = 0;
        ++j;

        MatRestoreRow(m_train_data_n,i,&ncols, &cols, &vals);
    }
#if dbl_SV_read_problem >= 5
    printf("[SV][RP] end of negative class, i is :%d,num of elements(j) is :%d\n",i,j);
#endif
//    Destroy p_data, n_data    ( They are deleted after 2nd stage of model selection )
}



void Solver::set_weights_num_points(svm_parameter& param_, PetscInt num_p_point, PetscInt num_n_point){
    param_.weight[0]= 1.0 / num_p_point;
    param_.weight[1]= 1.0 / num_n_point;
#if dbl_SV_SWNP >= 1
        printf("[SV][set_weights_num_points] Min class P(label :%d,\t # row:%d,\t weight:%g)\n",param_.weight_label[0], num_p_point, param_.weight[0]);
        printf("[SV][set_weights_num_points] Maj class N(label :%d,\t # row:%d,\t weight:%g)\n",param_.weight_label[1], num_n_point, param_.weight[1]);
#endif
}

void Solver::set_weights_sum_volume(svm_parameter& param_, Vec& v_vol_p, Vec& v_vol_n){
    PetscScalar sum_vol_p, sum_vol_n;
    VecSum(v_vol_p, &sum_vol_p);
    VecSum(v_vol_n, &sum_vol_n);
    param_.weight[0]= 1.0 / sum_vol_p;
    param_.weight[1]= 1.0 / sum_vol_n;
#if dbl_SV_SWSV >= 1
        printf("[SV][set_weights_sum_volume] Min class P(label :%d,\t Sum_volume:%d,\t weight:%g)\n",param_.weight_label[0], sum_vol_p, param_.weight[0]);
        printf("[SV][set_weights_sum_volume] Maj class N(label :%d,\t Sum_volume:%d,\t weight:%g)\n",param_.weight_label[1], sum_vol_n, param_.weight[1]);
#endif
}

void Solver::set_weights_sum_volume_index_base(svm_parameter& param_, Vec& v_vol_p, Vec& v_vol_n,
                                               std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                                               PetscInt iter_p_end, PetscInt iter_n_end ){  //@@
    PetscScalar sum_vol_p = 0, sum_vol_n=0;
    PetscInt i, all_vol_p_size, all_vol_n_size;
    VecGetSize(v_vol_p, &all_vol_p_size);
    VecGetSize(v_vol_n, &all_vol_n_size);

    PetscScalar     *arr_vol_p_, * arr_vol_n_;

    PetscMalloc1(all_vol_p_size, &arr_vol_p_);
    VecGetArray(v_vol_p,&arr_vol_p_);
    for(i=0 ; i < iter_p_end ; i++){
        sum_vol_p += arr_vol_p_[v_p_index[i]];
    }
    VecRestoreArray(v_vol_p,&arr_vol_p_);
    PetscFree(arr_vol_p_);

    PetscMalloc1(all_vol_n_size, &arr_vol_n_);
    VecGetArray(v_vol_n,&arr_vol_n_);
    for(i=0 ; i < iter_n_end ; i++){
        sum_vol_n += arr_vol_n_[v_n_index[i]];
    }
    VecRestoreArray(v_vol_n,&arr_vol_n_);
    PetscFree(arr_vol_n_);


    param_.weight[0]= 1.0 / sum_vol_p;
    param_.weight[1]= 1.0 / sum_vol_n;
#if dbl_SV_SWSVIB >= 1
        printf("[SV][set_weights_sum_volume] Min class P(label :%d,\t Sum_volume:%d,\t weight:%g)\n",param_.weight_label[0], sum_vol_p, param_.weight[0]);
        printf("[SV][set_weights_sum_volume] Maj class N(label :%d,\t Sum_volume:%d,\t weight:%g)\n",param_.weight_label[1], sum_vol_n, param_.weight[1]);
#endif
}




void Solver::PD_set_weights_sum_num_point_IB(svm_parameter& param_,std::vector<int>& v_target_lbl, const PetscScalar * arr_index, int num_nnz){  //@@
    PetscScalar sum_num_p = 0, sum_num_n=0;
    PetscInt i;

    for(i=0; i < num_nnz; ++i){
//        printf("[SV][PD_set_weights_sum_num_point_IB] i:%d, arr_index[i]:%g, v_target_lbl[arr_index[i]]:%d, s_p:%g, s_n:%g\n",
//                                                            i, arr_index[i], v_target_lbl[arr_index[i]],sum_num_p, sum_num_n);
        if (v_target_lbl[arr_index[i]] == 1 )
            sum_num_p++;
        else
            sum_num_n++;
    }

    param_.weight[0]= 1.0 / sum_num_p;
    param_.weight[1]= 1.0 / sum_num_n;
#if dbl_SV_PDSWSPIB >= 1
    printf("[SV][PD_set_weights_sum_num_point_IB] Min class P(label :%d,\t num_p_points:%g,\t weight:%g)\n",param_.weight_label[0], sum_num_p, param_.weight[0]);
    printf("[SV][PD_set_weights_sum_num_point_IB] Maj class N(label :%d,\t num_n_points:%g,\t weight:%g)\n",param_.weight_label[1], sum_num_n, param_.weight[1]);
#endif
}




void Solver::alloc_memory_for_weights(svm_parameter& in_param, bool free_first){
    if(free_first){
        free(in_param.weight_label);
        free(in_param.weight);
    }
    in_param.nr_weight = 2;
    in_param.weight_label = (int *) malloc(2 * sizeof(int));
    in_param.weight = (double *) malloc(2 * sizeof(double));
    in_param.weight_label[0] = 1;                      // Minority class (P)
    in_param.weight_label[1] = -1;                     // Maj            (N)
#if dbl_SV_malloc_weights >= 1
        printf("[SV][alloc_memory_for_weights] nr_weight:%d,weight_label[0]:%d, weight_label[1]:%d\n",
               in_param.nr_weight, in_param.weight_label[0], in_param.weight_label[1]);
#endif
}


//======================================================================
void Solver::read_parameters(){
    param.svm_type = Config_params::getInstance()->get_svm_svm_type();
    param.kernel_type = Config_params::getInstance()->get_svm_kernel_type();
    param.degree = Config_params::getInstance()->get_svm_degree();
    param.gamma = Config_params::getInstance()->get_svm_gamma();
//    param.coef0 = Config_params::getInstance()->get_svm_coef0();
//    param.nu = Config_params::getInstance()->get_svm_nu();
    param.cache_size = Config_params::getInstance()->get_svm_cache_size();
    param.C = Config_params::getInstance()->get_svm_C();
    param.eps = Config_params::getInstance()->get_svm_eps();
//    param.p = Config_params::getInstance()->get_svm_p();
    param.shrinking = Config_params::getInstance()->get_svm_shrinking();
    param.probability = Config_params::getInstance()->get_svm_probability();
    param.nr_weight = Config_params::getInstance()->get_svm_nr_weight();
    param.weight_label = NULL;
    param.weight = NULL;
}

void Solver::print_parameters(){
    PetscPrintf(PETSC_COMM_WORLD,"[SV][Print_Parameter] config params are: svm_type:%d\nkernel_type:%d\ndegree:%d\ngamma:%g\ncache_size(MB):%g\nC:%g\neps:%g\n",
                Config_params::getInstance()->get_svm_svm_type(), Config_params::getInstance()->get_svm_kernel_type(),
                Config_params::getInstance()->get_svm_degree(), Config_params::getInstance()->get_svm_gamma(),
                Config_params::getInstance()->get_svm_cache_size(), Config_params::getInstance()->get_svm_C(),
                Config_params::getInstance()->get_svm_eps());
    PetscPrintf(PETSC_COMM_WORLD,"[SV][Print_Parameter] local params which are set and checked are: svm_type:%d\nkernel_type:%d\ndegree:%d\ngamma:%g\ncache_size(MB):%g\nC:%g\neps:%g\n",
                param.svm_type, param.kernel_type,param.degree, param.gamma, param.cache_size, param.C, param.eps);
}










void Solver::evaluate_testdata(int level, summary& final_summary){
    Loader test_loader;
    Mat untouched_test_data ;
    untouched_test_data = test_loader.load_norm_data_sep(Config_params::getInstance()->get_test_ds_f_name() );
//    std::cout << "[SV][ETD]"  << Config_params::getInstance()->get_test_ds_f_name()  << std::endl;
    test_predict(untouched_test_data, final_summary);

    MatDestroy(&untouched_test_data);

    Config_params::getInstance()->print_summary(final_summary,"[SV][ETD]",level,-2);

//    if(level == 1){
//        Config_params::getInstance()->add_final_summary(final_summary);
//    }
}

/*
 * use the input matrix as testdata and not destroy the matrix
 */
void Solver::evaluate_testdata(Mat& untouched_test_data, int level, summary& final_summary){
//    Loader test_loader;
//    Mat untouched_test_data ;
//    untouched_test_data = test_loader.load_norm_data_sep(Config_params::getInstance()->get_test_ds_f_name() );
//    std::cout << "[SV][ETD]"  << Config_params::getInstance()->get_test_ds_f_name()  << std::endl;
    test_predict(untouched_test_data, final_summary);

//    MatDestroy(&untouched_test_data);

    Config_params::getInstance()->print_summary(final_summary,"[SV][ETD]",level,-2);

//    if(level == 1){
//        Config_params::getInstance()->add_final_summary(final_summary);
//    }
}





//std::map<measures,double> Solver::test_predict(Mat& test_data){
void Solver::test_predict(Mat& test_data, summary& result_summary, int iteration){
#if dbl_SV_test_predict >= 7
    printf("[SV][test_predict] test_predict_data Matrix:\n");                                       //$$debug
    MatView(test_data,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
#endif
    int correct = 0;
    int total = 0;
    double error = 0;
    double sump = 0, sumt = 0, sumpp = 0, sumtt = 0, sumpt = 0;
    double tp =0, tn =0, fp =0, fn=0;
    int svm_type=svm_get_svm_type(local_model);
//    std::cout <<"[SV][TP] svm_type is "<< svm_type <<"\n" ; //exit(1);
//    int nr_class=svm_get_nr_class(local_model);
    double *prob_estimates=NULL;
//    std::map<measures,double> results_;
//start of reading the test points
    PetscInt i=0, k=0, ncols=0,num_points=0,num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(test_data,&num_points,&num_col);   //get the number of data points
#if dbl_SV_test_predict >= 3
    printf("[SV][test_predict] test data points rows:%d cols:%d \n", num_points,num_col);
#endif
//    std::cout <<"[SV][TP] {2} \n" ; //exit(1);
//read test points from matrix to array
//    num_points = 1;                         /// only for debug $$
    for (i=0; i< num_points;i++){
        double target_label, predict_label;//target is the one that is in the vector
        MatGetRow(test_data,i,&ncols, &cols, &vals);

        target_label = vals[0];             //read the label in the first column(0)
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row

        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
#if dbl_SV_test_predict >= 9 // 9 default
            printf("[SV][test_predict] i:%d, k-1:%d, x[k-1].index:%d, x[k-1].value:%g\n",i, k-1,x[k-1].index, x[k-1].value);    //$$debug
#endif
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
#if dbl_SV_test_predict >= 9 // 9 default
        printf("[SV][test_predict] k:%d, x[k].index:%d, x[k].value:%g\n", k,x[k].index, x[k].value);    //$$debug
#endif
        MatRestoreRow(test_data,i,&ncols, &cols, &vals);

        if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))  {    // Not used
            predict_label = svm_predict_probability(local_model,x,prob_estimates);
        }
        else {
            predict_label = svm_predict(local_model,x);
        }

//        printf("[SV][test_predict] predicts %g\n", predict_label);    //$$debug
        if(target_label == 1){   //positive class
            if (predict_label == 1)     //correct
                tp++;
            else                                //predict negative
                fn++;                   //false
        }
        else{                    //negative class
            if (predict_label == -1)    //correct
                tn++;
            else                                //predict positive
                fp++;                   //false
        }


        error += (predict_label-target_label)*(predict_label-target_label);
        sump += predict_label;
        sumt += target_label;
        sumpp += predict_label * predict_label;
        sumtt += target_label  * target_label;
        sumpt += predict_label * target_label;
        ++total;
    }
    correct = tp+tn;                //sum both True

    result_summary.perf[Sens] = tp / (tp+fn) ;
    result_summary.perf[Spec] = tn / (tn+fp) ;
    result_summary.perf[Gmean] = sqrt(result_summary.perf[Sens] * result_summary.perf[Spec]);
    result_summary.perf[Acc] = (double)correct / (total) ;

    if(tp+fp == 0)              //prevent nan case
        result_summary.perf[PPV] = 0;
    else
        result_summary.perf[PPV] = tp/ (tp+fp);

    if(tn+fn == 0)              //prevent nan case
        result_summary.perf[NPV] = 0;
    else
        result_summary.perf[NPV] = tn/ (tn+fn);

    result_summary.perf[F1] = 2*tp / (2*tp+fp+fn);

    result_summary.perf[TP] = tp;
    result_summary.perf[FP] = fp;
    result_summary.perf[TN] = tn;
    result_summary.perf[FN] = fn;
    result_summary.C = local_model->param.C;
    result_summary.gamma = local_model->param.gamma;
    result_summary.num_SV_p = local_model->nSV[0];
    result_summary.num_SV_n = local_model->nSV[1];
    if(iteration != -1)             //later it is needed to select the best model
        result_summary.iter = iteration;
#if dbl_SV_test_predict >= 1    // 1 default
    Config_params::getInstance()->print_summary(result_summary,"[SV][TP]");
#endif
//    std::cout <<"[SV][TP] {4} \n" ; exit(1);
//end of loop for each test point
    if (svm_type==NU_SVR || svm_type==EPSILON_SVR)       {
        printf("Mean squared error = %g (regression)\n",error/total);
        printf("Squared correlation coefficient = %g (regression)\n",
            ((total*sumpt-sump*sumt)*(total*sumpt-sump*sumt))/
            ((total*sumpp-sump*sump)*(total*sumtt-sumt*sumt))             );
    }
    else{
#if dbl_SV_test_predict >= 1
//        printf("Accuracy = %.2f%% (%d/%d) (classification)\n",                  (double)correct/total*100,correct,total);
#endif
    }
    if(predict_probability)
        free(prob_estimates);
}





void Solver::predict_validation_data(Mat& m_VD_p,Mat& m_VD_n, summary& result_summary, int iteration){
#if dbl_SV_predict_VD >= 7
    printf("[SV][Predict_VD] m_VD_p Matrix:\n");                              //$$debug
    MatView(m_VD_p,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
    printf("[SV][Predict_VD] m_VD_n Matrix:\n");                              //$$debug
    MatView(m_VD_n,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
#endif
    ETimer t_predict_VD;
    int correct = 0;
    double tp =0, tn =0, fp =0, fn=0;
    PetscInt    i=0, k=0, ncols=0,num_points_p=0,num_points_n=0,num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;
    double target_label = 0, predict_label=0;

    MatGetSize(m_VD_p,&num_points_p,&num_col);   //get the number of data points in positive class
    MatGetSize(m_VD_n,&num_points_n,NULL);       //get the number of data points in negative class
#if dbl_SV_predict_VD >= 3
    printf("[SV][Predict_VD] m_VD_p rows:%d cols:%d \n", num_points_p,num_col);
    printf("[SV][Predict_VD] m_VD_n rows:%d cols:%d \n", num_points_p,num_col);
#endif

    // - - - - - positive class - - - - -
    target_label = 1;
    for (i=0; i< num_points_p;i++){
        MatGetRow(m_VD_p,i,&ncols, &cols, &vals);
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=0; k< ncols; k++) {  // data starts at zero
            x[k].index = cols[k]+1;   //validation data doesn't have the label, so I need to add 1 to indices
            x[k].value = vals[k];
        }
        x[k].index = -1;    //create the end element of each node (-1,0)
        x[k].value = 0;
        MatRestoreRow(m_VD_p,i,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);
        if (predict_label == 1)     //correct
            tp++;
        else                                //predict negative
            fn++;                   //false
    }
    // - - - - - negative class - - - - -@@
    target_label = -1;
    for (i=0; i< num_points_n;i++){
        MatGetRow(m_VD_n,i,&ncols, &cols, &vals);
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=0; k< ncols; k++) {  // data starts at zero
            x[k].index = cols[k]+1;   //validation data doesn't have the label, so I need to add 1 to indices
            x[k].value = vals[k];
        }
        x[k].index = -1;    //create the end element of each node (-1,0)
        x[k].value = 0;
        MatRestoreRow(m_VD_n,i,&ncols, &cols, &vals);
        predict_label = svm_predict(local_model,x);
        if (predict_label == -1)    //correct
            tn++;
        else                                //predict positive
            fp++;                   //false
    }

    // - - - - - calc performance measures - - - - -
    correct = tp+tn;                //sum both True
    result_summary.perf[Sens] = tp / (tp+fn) ;
    result_summary.perf[Spec] = tn / (tn+fp) ;
    result_summary.perf[Gmean] = sqrt(result_summary.perf[Sens] * result_summary.perf[Spec]);
    result_summary.perf[Acc] = (double)correct / (num_points_p + num_points_n) ;
    if(tp+fp == 0)              //prevent nan case
        result_summary.perf[PPV] = 0;
    else
        result_summary.perf[PPV] = tp/ (tp+fp);

    if(tn+fn == 0)              //prevent nan case
        result_summary.perf[NPV] = 0;
    else
        result_summary.perf[NPV] = tn/ (tn+fn);

    result_summary.perf[F1] = 2*tp / (2*tp+fp+fn);

    result_summary.perf[TP] = tp;
    result_summary.perf[FP] = fp;
    result_summary.perf[TN] = tn;
    result_summary.perf[FN] = fn;
    result_summary.C = local_model->param.C;
    result_summary.gamma = local_model->param.gamma;
    result_summary.num_SV_p = local_model->nSV[0];
    result_summary.num_SV_n = local_model->nSV[1];
    if(iteration != -1)             //later it is needed to select the best model
        result_summary.iter = iteration;
#if dbl_SV_predict_VD >= 1    // 1 default
    Config_params::getInstance()->print_summary(result_summary,"[SV][TP]");
#endif
#if dbl_SV_predict_VD_rpt_time >= 1    // 0 default
    t_predict_VD.stop_timer("predict VD");
#endif
}



















void Solver::test_predict_index_base(Mat& m_data_p, Mat& m_data_n,
                                               std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                                               PetscInt iter_p_end, PetscInt iter_n_end, summary& result_summary, int iteration){

    int correct = 0;
    double tp =0, tn =0, fp =0, fn=0;

    //start of reading the test points
    PetscInt i=0, k=0, ncols=0,num_points_p=0, num_points_n=0, num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(m_data_p,NULL,&num_col);
    // * * *  number of points in the index vector, the number of points in matrix is not related to index base functions * * *
    num_points_p = v_p_index.size();
    num_points_n = v_n_index.size();
#if dbl_SV_TPIB >= 3
    printf("[SV][test_predict_index_base] test data points rows:%d cols:%d \n", num_points,num_col);
#endif

    double predict_label;
    //read test points P from matrix to array
    for (i=iter_p_end; i< num_points_p;i++){
//        double target_label, predict_label;//target is the one that is in the vector
        MatGetRow(m_data_p,i,&ncols, &cols, &vals);

//        target_label = +1;
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
        MatRestoreRow(m_data_p,i,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);

        if (predict_label == 1)     //correct
            tp++;
        else                                //predict negative
            fn++;                   //false
    }
    //read test points N from matrix to array
    for (i=iter_n_end; i< num_points_n;i++){
//        double target_label, predict_label;//target is the one that is in the vector
        MatGetRow(m_data_n,i,&ncols, &cols, &vals);

//        target_label = -1;
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
        MatRestoreRow(m_data_n,i,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);

        if (predict_label == -1)    //correct
            tn++;
        else                                //predict positive
            fp++;                   //false
    }


    correct = tp+tn;                //sum both True

    result_summary.perf[Sens] = tp / (tp+fn) ;
    result_summary.perf[Spec] = tn / (tn+fp) ;
    result_summary.perf[Gmean] = sqrt(result_summary.perf[Sens] * result_summary.perf[Spec]);
    result_summary.perf[Acc] = (double)correct / (tp+ fn + tn + fp) ;

    if(tp+fp == 0)              //prevent nan case
        result_summary.perf[PPV] = 0;
    else
        result_summary.perf[PPV] = tp/ (tp+fp);

    if(tn+fn == 0)              //prevent nan case
        result_summary.perf[NPV] = 0;
    else
        result_summary.perf[NPV] = tn/ (tn+fn);


    result_summary.perf[F1] = 2*tp / (2*tp+fp+fn);

    result_summary.perf[TP] = tp;
    result_summary.perf[FP] = fp;
    result_summary.perf[TN] = tn;
    result_summary.perf[FN] = fn;

    result_summary.C = local_model->param.C;
    result_summary.gamma = local_model->param.gamma;

    result_summary.num_SV_p = 0;
    result_summary.num_SV_n = 0;

    if(iteration != -1)             //later it is needed to select the best model
        result_summary.iter = iteration;
#if dbl_SV_TPIB >= 1
    Config_params::getInstance()->print_summary(result_summary, "[SV][TPIB]");
#endif
//    return results_;
}





void Solver::test_predict_index_base_separate_validation(Mat& m_data_p, Mat& m_data_n,
                                               std::vector<PetscInt>& v_p_index, std::vector<PetscInt>& v_n_index,
                                               PetscInt iter_p_end, PetscInt iter_n_end, summary& result_summary, int iteration,
                                               Mat& m_VD_p, Mat& m_VD_n){

    int correct = 0;
    double tp =0, tn =0, fp =0, fn=0;

    //start of reading the test points
    PetscInt i=0, k=0, ncols=0,num_points_p=0, num_points_n=0, num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(m_data_p,NULL,&num_col);
    // * * *  number of points in the index vector, the number of points in matrix is not related to index base functions * * *
    num_points_p = v_p_index.size();
    num_points_n = v_n_index.size();
#if dbl_SV_TPIB >= 3
    printf("[SV][test_predict_index_base] test data points rows:%d cols:%d \n", num_points,num_col);
#endif

    double predict_label;
    //read test points P from matrix to array
    for (i=iter_p_end; i< num_points_p;i++){
        MatGetRow(m_data_p,i,&ncols, &cols, &vals);

        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
        MatRestoreRow(m_data_p,i,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);

        if (predict_label == 1)     //correct
            tp++;
        else                                //predict negative
            fn++;                   //false
    }
    //read test points N from matrix to array
    for (i=iter_n_end; i< num_points_n;i++){
//        double target_label, predict_label;//target is the one that is in the vector
        MatGetRow(m_data_n,i,&ncols, &cols, &vals);

//        target_label = -1;
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
        MatRestoreRow(m_data_n,i,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);

        if (predict_label == -1)    //correct
            tn++;
        else                                //predict positive
            fp++;                   //false
    }


    correct = tp+tn;                //sum both True

    result_summary.perf[Sens] = tp / (tp+fn) ;
    result_summary.perf[Spec] = tn / (tn+fp) ;
    result_summary.perf[Gmean] = sqrt(result_summary.perf[Sens] * result_summary.perf[Spec]);
    result_summary.perf[Acc] = (double)correct / (tp+ fn + tn + fp) ;

    if(tp+fp == 0)              //prevent nan case
        result_summary.perf[PPV] = 0;
    else
        result_summary.perf[PPV] = tp/ (tp+fp);

    if(tn+fn == 0)              //prevent nan case
        result_summary.perf[NPV] = 0;
    else
        result_summary.perf[NPV] = tn/ (tn+fn);


    result_summary.perf[F1] = 2*tp / (2*tp+fp+fn);

    result_summary.perf[TP] = tp;
    result_summary.perf[FP] = fp;
    result_summary.perf[TN] = tn;
    result_summary.perf[FN] = fn;

    result_summary.C = local_model->param.C;
    result_summary.gamma = local_model->param.gamma;

    result_summary.num_SV_p = 0;
    result_summary.num_SV_n = 0;

    if(iteration != -1)             //later it is needed to select the best model
        result_summary.iter = iteration;
#if dbl_SV_TPIB >= 1
    Config_params::getInstance()->print_summary(result_summary, "[SV][TPIB]");
#endif
//    return results_;
}



/* target_row is the classifier_id in ModelSelection::uniform_design_index_base_separate_validation
 *
 * target_row identifies the row of the m_predicted_label to store the prediction
 * the column index is the loop index
 * the value is the prediction value from model
 */

void Solver::predict_test_data_in_matrix_output(Mat& test_data, int target_row, Mat& m_predicted_label){
#if dbl_SV_predict_label1 >= 7
    printf("[SV][test_predict] predict_label1 test_data Matrix:\n");                                       //$$debug
    MatView(test_data,PETSC_VIEWER_STDOUT_WORLD);                                //$$debug
#endif
    int svm_type=svm_get_svm_type(local_model);
    double *prob_estimates=NULL;
//start of reading the test points
    PetscInt i=0, k=0, ncols=0,num_points=0,num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(test_data,&num_points,&num_col);   //get the number of data points
#if dbl_SV_predict_label1 >= 3
    printf("[SV][test_predict] test data points rows:%d cols:%d \n", num_points,num_col);
#endif
//read test points from matrix to array
    for (i=0; i< num_points;i++){
        double target_label, predict_label;//target is the one that is in the vector
        MatGetRow(test_data,i,&ncols, &cols, &vals);

        target_label = vals[0];             //read the label in the first column(0)
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //No need for +1 as I add the label in index 0 that add one non-zero value to each row
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
#if dbl_SV_predict_label1 >= 9
            printf("[SV][PL] i:%d, k-1:%d, x[k-1].index:%d, x[k-1].value:%g\n",i, k-1,x[k-1].index, x[k-1].value);    //$$debug
#endif
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
#if dbl_SV_predict_label1 >= 9
        printf("[SV][PL] end element k:%d, x[k].index:%d, x[k].value:%g\n", k,x[k].index, x[k].value);    //$$debug
#endif
        MatRestoreRow(test_data,i,&ncols, &cols, &vals);

        if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))  {    // Not used
            predict_label = svm_predict_probability(local_model,x,prob_estimates);
        }else {
            predict_label = svm_predict(local_model,x);
        }

#if dbl_SV_predict_label1 >= 3
        printf("[SV][PL1] target_row:%d, i:%d, target_label:%g, predict_label:%g\n", target_row, i, target_label, predict_label);    //$$debug
#endif

        MatSetValue(m_predicted_label,target_row,i,(PetscScalar)predict_label,INSERT_VALUES);
    }

    if(predict_probability)
        free(prob_estimates);
}




/*
 * number of rows in the output matrix (m_predicted_label) are equal to number of group of partitions in refinement at the level which called this function
 * However, we only focus on a specific row which is target_row which is related to current solver's model
 * The number of columns in the output matrix (m_predicted_label) is the number of points in both m_VD_p and m_VD_n
 * Notice: there is no label in the beginning of the m_VD_X matrices compare to test_data matrix in similar methods
 */

void Solver::predict_VD_in_output_matrix(Mat& m_VD_p,Mat& m_VD_n, int target_row, Mat& m_predicted_label){
    int svm_type=svm_get_svm_type(local_model);
    double *prob_estimates=NULL;
//start of reading the VD_p points
    PetscInt i=0, k=0, ncols=0,num_points_p=0, num_points_n=0, num_col=0;
    const PetscInt    *cols;
    const PetscScalar *vals;

    MatGetSize(m_VD_p,&num_points_p,&num_col);   //get the number of VD positive
    MatGetSize(m_VD_n,&num_points_n,&num_col);   //get the number of VD negative
#if dbl_SV_predict_label1 >= 3
    printf("[SV][test_predict] VD p rows:%d, VD n rows:%d,  cols:%d \n", num_points_p, num_points_n, num_col);
#endif
    //read m_VD_p points from matrix to array
    for (i=0; i< num_points_p;i++){
        double predict_label;
        MatGetRow(m_VD_p,i,&ncols, &cols, &vals);
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //+1 is needed since the label is not in index 0
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        x[k].index = -1;    //create the end element of each node (-1,0)
        x[k].value = 0;
        MatRestoreRow(m_VD_p,i,&ncols, &cols, &vals);

        if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))  {    // Not used
            predict_label = svm_predict_probability(local_model,x,prob_estimates);
        }else {
            predict_label = svm_predict(local_model,x);     //main method probably
        }
        MatSetValue(m_predicted_label,target_row,i,(PetscScalar)predict_label,INSERT_VALUES);
    }

    //read m_VD_p points from matrix to array
    for (i=0; i< num_points_n;i++){
        double predict_label;
        MatGetRow(m_VD_n,i,&ncols, &cols, &vals);
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));    //+1 is needed since the label is not in index 0
        for (k=1; k< ncols; k++) {  //zero is the label
            x[k-1].index = cols[k];   //since I add the label in the first column +1 is not required anymore
            x[k-1].value = vals[k];
        }
        x[k].index = -1;    //create the end element of each node (-1,0)
        x[k].value = 0;
        MatRestoreRow(m_VD_n,i,&ncols, &cols, &vals);

        if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))  {    // Not used
            predict_label = svm_predict_probability(local_model,x,prob_estimates);
        }else {
            predict_label = svm_predict(local_model,x);     //main method probably
        }
        MatSetValue(m_predicted_label,target_row,num_points_p + i,(PetscScalar)predict_label,INSERT_VALUES);   //num_points_p is added to adjust the column index (shift the column to right part of the matrix)
    }

    if(predict_probability)
        free(prob_estimates);
}











void Solver::PD_test_predict_index_base(Mat& m_data, std::vector<PetscInt> v_target_lbl, const PetscScalar * arr_train_index,
                            PetscInt idx_start_test, PetscInt idx_end_test, summary& result_summary, int iteration){

    int correct = 0;
    double tp =0, tn =0, fp =0, fn=0;

    //start of reading the test points
    PetscInt i=0, k=0, ncols=0, num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(m_data,NULL,&num_col);

    double predict_label;
    //read test points from matrix to array
    for (i=idx_start_test; i< idx_end_test;i++){
        double target_label, predict_label;         //target is the one that is in the vector v_target_lbl
        int idx_test_data = arr_train_index[i];     // the index for test data point
        MatGetRow(m_data, idx_test_data,&ncols, &cols, &vals);

        target_label = v_target_lbl[idx_test_data];        // real label
        //allocate required memory
        x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));
        for (k=0; k< ncols; k++) {  //zero is the label
            // x[0].index == 1 , in another word, the index starts with 1, but array of index fills from zero
            x[k].index = cols[k]+1;   // PD test data doesn't have the label, so I need to add 1 to indices
            x[k].value = vals[k];
        }
        //create the end element of each node (-1,0)
        x[k].index = -1;
        x[k].value = 0;
        MatRestoreRow(m_data, idx_test_data,&ncols, &cols, &vals);

        predict_label = svm_predict(local_model,x);


        if(target_label == 1){   //positive class
            if (predict_label == 1)     //correct
                tp++;
            else                                //predict negative
                fn++;                   //false
        }
        else{                    //negative class
            if (predict_label == -1)    //correct
                tn++;
            else                                //predict positive
                fp++;                   //false
        }
    }

    correct = tp+tn;                //sum both True

    result_summary.perf[Sens] = tp / (tp+fn) ;
    result_summary.perf[Spec] = tn / (tn+fp) ;
    result_summary.perf[Gmean] = sqrt(result_summary.perf[Sens] * result_summary.perf[Spec]);
    result_summary.perf[Acc] = (double)correct / (tp+ fn + tn + fp) ;

    if(tp+fp == 0)              //prevent nan case
        result_summary.perf[PPV] = 0;
    else
        result_summary.perf[PPV] = tp/ (tp+fp);

    if(tn+fn == 0)              //prevent nan case
        result_summary.perf[NPV] = 0;
    else
        result_summary.perf[NPV] = tn/ (tn+fn);


    result_summary.perf[F1] = 2*tp / (2*tp+fp+fn);

    result_summary.perf[TP] = tp;
    result_summary.perf[FP] = fp;
    result_summary.perf[TN] = tn;
    result_summary.perf[FN] = fn;

    result_summary.C = local_model->param.C;
    result_summary.gamma = local_model->param.gamma;

    result_summary.num_SV_p = 0;
    result_summary.num_SV_n = 0;

    if(iteration != -1)             //later it is needed to select the best model
        result_summary.iter = iteration;
#if dbl_SV_PDTPIB >= 1
    Config_params::getInstance()->print_summary(result_summary, "[SV][PD_TP_IB]");
#endif
}




/*
 * Notice the data is not contains the labels at all
 */
int Solver::PD_predict_a_label(Mat& m_data, int target_row){
    int svm_type=svm_get_svm_type(local_model);

//start of reading the test points
    PetscInt i=0, k=0, ncols=0,num_points=0,num_col=0;
    const PetscInt    *cols;                        //if not NULL, the column numbers
    const PetscScalar *vals;

    MatGetSize(m_data,NULL,&num_col);   //get the number of columns
#if dbl_SV_PD_predict_a_label >= 3
    printf("[SV][PD_predict_a_label] number of columns in test data point at row:%d is %d \n", target_row, num_col);
#endif

    // - - - - - - - READ THE Test Point - - - - - No label - - - -
    double predict_label;//target is the one that is in the vector
    MatGetRow(m_data,target_row,&ncols, &cols, &vals);

    //allocate required memory
    //+1 is needed as the label is not added in index 0
    x = (struct svm_node *) realloc(x, (ncols + 1 ) * sizeof(struct svm_node));
    for (k=0; k< ncols; k++) {  //zero is not the label (different from other predict methods)
        x[k].index = cols[k]+1;         // x[0].index == 1 , in another word, the index starts with 1, but array of index fills from zero
        x[k].value = vals[k];           // value remains untouched
#if dbl_SV_PD_predict_a_label >= 9
        printf("[SV][PD_predict_a_label] i:%d, k:%d, x[k].index:%d, x[k].value:%g\n",i, k,x[k].index, x[k].value);    //$$debug
#endif
    }
    //create the end element of each node (-1,0)
    x[k].index = -1;
    x[k].value = 0;
#if dbl_SV_PD_predict_a_label >= 9
    printf("[SV][PD_predict_a_label] end element k:%d, x[k].index:%d, x[k].value:%g\n", k,x[k].index, x[k].value);    //$$debug
#endif
    MatRestoreRow(m_data,target_row,&ncols, &cols, &vals);

    predict_label = svm_predict(local_model,x);

//    std::cout << "[SV][PD_predict_a_label] id:" << target_row << " predict:" << predict_label << std::endl;
#if dbl_SV_PD_predict_a_label >= 3
        printf("[SV][PD_predict_a_label] predict_label:%g\n", predict_label);    //$$debug
#endif
    return predict_label;
}




void Solver::prepare_solution_single_model(svm_model * model_, int num_point_p, solution& sol_single_model){
    PetscInt i;
    sol_single_model.C = model_->param.C;
    sol_single_model.gamma = model_->param.gamma;

#if dbl_SV_PSSM >= 3
    printf("\n\n[SV][PSSM] 1st SV in minority (model_->sv_indices[0]):%d\n", model_->sv_indices[0]);
    printf("[SV][PSSM] 1st SV in majority (model_->sv_indices[model_->nSV[0] ] - 1 - num_point_p) :%d, num_points_p:%d\n",
           model_->sv_indices[model_->nSV[0] ] - 1 - num_point_p, num_point_p);
    #if dbl_SV_PSSM >= 5
        printf("indices of support vectors for minority class:\n");
        for (int k=0; k < model_->nSV[0];k++){
            printf("%d,", model_->sv_indices[k]);
        }
        printf("\nindices of support vectors for mijority class:\n");
        for (int k=0; k < model_->nSV[1];k++){
            printf("%d,", model_->sv_indices[model_->nSV[0] + k]);
        }
        printf("\n");

        printf("model_->sv_indices[k]:\n");
        for (int k=0; k < model_->l;k++){
            printf("%d,", model_->sv_indices[k]);
        }
        printf("\n");
    #endif
#endif

    sol_single_model.p_index.reserve(model_->nSV[0] );
    for (i=0; i < model_->nSV[0];i++){
        // -1 because sv_indice start from 1, while petsc row start from 0
        sol_single_model.p_index.push_back(model_->sv_indices[i] - 1);
    }

    sol_single_model.n_index.reserve(model_->nSV[1] );
    // add the index in the model for it after subtract from number of minority in training data
    for (int i=0; i < model_->nSV[1];i++){
        // -1 the same as pos class, p_num_row because they are after each other

//        sol_single_model.n_index.push_back(model_->sv_indices[model_->nSV[0] + i] - 1 - num_point_p); //for normal libsvm
        sol_single_model.n_index.push_back(model_->sv_indices[model_->nSV[0] + i] - 1 - num_point_p); //for instance weighted libsvm

    }

#if dbl_SV_PSSM >= 3
    printf("\n\n[SV][PSSM] solution has nSV+:%d, nSV-:%d, l:%d\n", model_->nSV[0], model_->nSV[1], model_->l);
#endif

#if export_SVM_models       //export the models
    //save the models in a local folder
    //use dataset name, experiment id, level id, index 0 for a single model (for multiple models, increament the id)
    //append the model to summary file after each export
    //make sure to close and open the summary file at each level to prevent losing models in the case of crash or error
    std::string output_file = "./svm_models/" + Config_params::getInstance()->get_ds_name()+
            "_exp_" + std::to_string(Config_params::getInstance()->get_main_current_exp_id()) +
            "_kf_" + std::to_string(Config_params::getInstance()->get_main_current_kf_id()) +
            "_level_" + std::to_string(Config_params::getInstance()->get_main_current_level_id()) + ".svmmodel";
    svm_save_model(output_file.c_str(), local_model);
    printf("model %s is saved\n", output_file.c_str());
#endif


}






void Solver::get_separate_matrices(Mat& m_data, std::vector<int>& v_target_lbl,
                                   const PetscScalar * arr_train_index, PetscInt num_nnz,
                                         Mat& m_p_data, Mat& m_n_data){
    IS              is_min_, is_maj_;
    PetscInt        i, min_cnt_=0, maj_cnt_=0;
    PetscInt        * ind_min_, * ind_maj_;
    PetscMalloc1(num_nnz, &ind_min_);
    PetscMalloc1(num_nnz, &ind_maj_);
    for(i=0; i< num_nnz; ++i){
        if(v_target_lbl[arr_train_index[i]] == 1){
            ind_min_[min_cnt_] = arr_train_index[i];
            min_cnt_++;
        }else{
            ind_maj_[maj_cnt_] = arr_train_index[i];
            maj_cnt_++;
        }
    }
    std::sort(ind_min_,ind_min_ + min_cnt_);   //this is critical for MatGetSubMatrix method
    std::sort(ind_maj_,ind_maj_ + maj_cnt_);   //this is critical for MatGetSubMatrix method

    ISCreateGeneral(PETSC_COMM_SELF,min_cnt_,ind_min_,PETSC_COPY_VALUES,&is_min_);
    ISCreateGeneral(PETSC_COMM_SELF,maj_cnt_,ind_maj_,PETSC_COPY_VALUES,&is_maj_);

    PetscFree(ind_min_);
    PetscFree(ind_maj_);

    MatGetSubMatrix(m_data,is_min_, NULL,MAT_INITIAL_MATRIX, &m_p_data);
    ISDestroy(&is_min_);

    MatGetSubMatrix(m_data,is_maj_, NULL,MAT_INITIAL_MATRIX, &m_n_data);
    ISDestroy(&is_maj_);

}
