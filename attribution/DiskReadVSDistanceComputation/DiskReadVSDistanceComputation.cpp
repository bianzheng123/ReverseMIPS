//
// Created by BianZheng on 2022/3/16.
//

#include "alg/SpaceInnerProduct.hpp"
#include "util/TimeMemory.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <memory>
#include <fstream>
#include <spdlog/spdlog.h>

using namespace std;
using namespace ReverseMIPS;

std::unique_ptr<double[]> GenRandom(const int &n_eval, const int &n_dim) {
    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> dis(1.0, 1000.0);

    std::unique_ptr<double[]> random_l = make_unique<double[]>(n_eval * n_dim);

    for (int itemID = 0; itemID < n_eval; itemID++) {
        for (int dim = 0; dim < n_dim; dim++) {
            int id = itemID * n_dim + dim;
            double random = dis(gen);
            random_l[id] = random;
        }
    }
    return random_l;
}

void
BuildWriteIndex(const char *index_path, const double *vecs1, const double *vecs2, const int &n_eval, const int &dim) {

    size_t write_size = n_eval * n_eval;
    std::vector<double> write_array(write_size);
#pragma omp parallel for default(none) shared(n_eval, vecs1, vecs2, write_array, dim)
    for (int xID = 0; xID < n_eval; xID++) {
        const double *x_vecs = vecs1 + xID * dim;
        for (int yID = 0; yID < n_eval; yID++) {
            const double *y_vecs = vecs2 + yID * dim;
            double ip = InnerProduct(x_vecs, y_vecs, dim);
            int id = xID * n_eval + yID;
            write_array[id] = ip;
        }
    }

    //build and write index
    std::ofstream out(index_path, std::ios::binary | std::ios::out);
    if (!out) {
        spdlog::error("error in write result");
    }

    out.write((char *) write_array.data(), n_eval * n_eval * sizeof(double));
}

int main(int argc, char **argv) {
    vector<int> dim_l{10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200};
    const int n_eval = 1000000;
    const char *index_path = "../../index/DiskRead.index";

    for (const int &dimension: dim_l) {
        std::unique_ptr<double[]> vecs1 = GenRandom(n_eval, dimension);
        std::unique_ptr<double[]> vecs2 = GenRandom(n_eval, dimension);
        std::unique_ptr<double[]> res = make_unique<double[]>(n_eval * n_eval);

        TimeRecord record;
        record.reset();
        for (int xID = 0; xID < n_eval; xID++) {
            double *x_vecs = vecs1.get() + xID * dimension;
            for (int yID = 0; yID < n_eval; yID++) {
                double *y_vecs = vecs2.get() + yID * dimension;
                double ip = InnerProduct(x_vecs, y_vecs, dimension);
                int id = xID * n_eval + yID;
                res[id] = ip;
            }
        }
        double comp_time = record.get_elapsed_time_second();

        BuildWriteIndex(index_path, vecs1.get(), vecs2.get(), n_eval, dimension);

        std::vector<double> read_array(n_eval * n_eval);
        std::ifstream in(index_path, std::ios::binary | std::ios::in);
        size_t read_size = (size_t) n_eval * n_eval * sizeof(double);
        record.reset();
        in.read((char *) read_array.data(), read_size);
        double read_disk_time = record.get_elapsed_time_second();
        spdlog::info("dimension {}, computation time {}, read disk time{}", dimension, comp_time, read_disk_time);
    }

    return 0;
}
