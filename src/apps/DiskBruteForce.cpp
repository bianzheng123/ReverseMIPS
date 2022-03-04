//
// Created by BianZheng on 2022/2/25.
//

#include "util/VectorIO.hpp"
#include "util/TimeMemory.hpp"
#include "util/FileIO.hpp"
#include "struct/UserRankElement.hpp"
#include "struct/VectorMatrix.hpp"
#include "DiskBruteForce.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <vector>
#include <string>
#include <map>

using namespace std;
using namespace ReverseMIPS;

int main(int argc, char **argv) {
    if (!(argc == 2 or argc == 3)) {
        cout << argv[0] << " dataset_name [basic_dir]" << endl;
        return 0;
    }
    const char *dataset_name = argv[1];
    const char *basic_dir = "/home/bianzheng/Dataset/ReverseMIPS";
    if (argc == 3) {
        basic_dir = argv[2];
    }
    spdlog::info("DiskBruteForce dataset_name {}, basic_dir {}", dataset_name, basic_dir);

    double total_build_index_time;
    char index_path[256];
    sprintf(index_path, "../index/%s.index", dataset_name);

    int n_data_item, n_query_item, n_user, vec_dim;
    vector<VectorMatrix> data = readData(basic_dir, dataset_name, n_data_item, n_query_item, n_user,
                                         vec_dim);
    VectorMatrix &user = data[0];
    VectorMatrix &data_item = data[1];
    VectorMatrix &query_item = data[2];
    spdlog::info("n_data_item {}, n_query_item {}, n_user {}, vec_dim {}", n_data_item, n_query_item, n_user, vec_dim);

    TimeRecord record;
    record.reset();
    DiskBruteForce::Index &index = DiskBruteForce::BuildIndex(data_item, user, index_path);
    total_build_index_time = record.get_elapsed_time_second();
    spdlog::info("finish preprocess and save the index");

    vector<int> topk_l{10, 20, 30, 40, 50};
    vector<DiskBruteForce::RetrievalResult> retrieval_res_l;
    vector<vector<vector<UserRankElement>>> result_rank_l;
    for (int topk: topk_l) {
        record.reset();
        vector<vector<UserRankElement>> result_rk = index.Retrieval(query_item, topk);

        double retrieval_time = record.get_elapsed_time_second();
        double read_disk_time = index.read_disk_time_;
        double inner_product_time = index.inner_product_time_;
        double binary_search_time = index.binary_search_time_;
        double second_per_query = retrieval_time / n_query_item;

        result_rank_l.emplace_back(result_rk);
        retrieval_res_l.emplace_back(retrieval_time, read_disk_time, inner_product_time, binary_search_time,
                                     second_per_query, topk);
    }

    spdlog::info("build index time: total %.3fs", total_build_index_time);
    int n_topk = (int) topk_l.size();

    for (int i = 0; i < n_topk; i++) {
        cout << retrieval_res_l[i].ToString() << endl;
        writeRank(result_rank_l[i], dataset_name, "DiskBruteForce");
    }

    map<string, string> performance_m;
    performance_m.emplace("build index total time", double2string(total_build_index_time));
    for (int i = 0; i < n_topk; i++) {
        retrieval_res_l[i].AddMap(performance_m);
    }
    writePerformance(dataset_name, "DiskBruteForce", performance_m);

    return 0;
}