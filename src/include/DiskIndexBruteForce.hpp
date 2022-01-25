//
// Created by BianZheng on 2021/12/22.
//

#ifndef REVERSE_KRANKS_DISKINDEXBRUTEFORCE_HPP
#define REVERSE_KRANKS_DISKINDEXBRUTEFORCE_HPP

#include "util/VectorIO.hpp"
#include "struct/VectorMatrix.hpp"
#include "struct/DistancePair.hpp"
#include "alg/SpaceInnerProduct.hpp"
#include "util/TimeMemory.hpp"
#include <fstream>
#include <vector>
#include <algorithm>
#include <cassert>

namespace ReverseMIPS {

    int write_every_ = 10000;
    const int report_batch_every_ = 5;

    /*
     * bruteforce index
     * shape: 1, type: int, n_data_item
     * shape: n_user * n_data_item, type: DistancePair, the distance pair for each user
     */

    double BuildSaveIndex(const VectorMatrix &data_item, const VectorMatrix &user, const char *index_path) {
        std::ofstream out(index_path, std::ios::binary | std::ios::out);
        if (!out) {
            std::printf("error in write result\n");
        }
        const int n_data_item = data_item.n_vector_;
        std::vector<DistancePair> distance_cache(write_every_ * n_data_item);
        const int vec_dim = data_item.vec_dim_;
        const int n_batch = user.n_vector_ / write_every_;
        const int n_remain = user.n_vector_ % write_every_;
        out.write((char *) &n_data_item, sizeof(int));

        TimeRecord record, batch_report_record;
        double preprocess_time_s = 0;
        batch_report_record.reset();
        for (int i = 0; i < n_batch; i++) {
            record.reset();
            for (int cacheID = 0; cacheID < write_every_; cacheID++) {
                int userID = write_every_ * i + cacheID;
                for (int itemID = 0; itemID < n_data_item; itemID++) {
                    double ip = InnerProduct(data_item.getVector(itemID), user.getVector(userID), vec_dim);
                    distance_cache[cacheID * n_data_item + itemID] = DistancePair(ip, itemID);
                }
                std::sort(distance_cache.begin() + cacheID * n_data_item,
                          distance_cache.begin() + (cacheID + 1) * n_data_item, std::greater<DistancePair>());
            }
            preprocess_time_s += record.get_elapsed_time_second();
            out.write((char *) distance_cache.data(), distance_cache.size() * sizeof(DistancePair));

            if (i % report_batch_every_ == 0) {
                std::cout << "preprocessed " << i / (0.01 * n_batch) << " %, "
                          << batch_report_record.get_elapsed_time_second() << " s/iter" << " Mem: "
                          << get_current_RSS() / 1000000 << " Mb \n";
                batch_report_record.reset();
            }

        }

        record.reset();
        for (int cacheID = 0; cacheID < n_remain; cacheID++) {
            int userID = cacheID + write_every_ * n_batch;
            for (int itemID = 0; itemID < data_item.n_vector_; itemID++) {
                double ip = InnerProduct(data_item.rawData_ + itemID * vec_dim,
                                        user.rawData_ + userID * vec_dim, vec_dim);
                distance_cache[cacheID * data_item.n_vector_ + itemID] = DistancePair(ip, itemID);
            }

            std::sort(distance_cache.begin() + cacheID * n_data_item,
                      distance_cache.begin() + (cacheID + 1) * n_data_item, std::greater<DistancePair>());

        }
        preprocess_time_s += record.get_elapsed_time_second();

        out.write((char *) distance_cache.data(),
                  n_remain * data_item.n_vector_ * sizeof(DistancePair));
        return preprocess_time_s;
    }

    class DiskIndexBruteForce {
    public:
        VectorMatrix user_;
        std::ifstream index_stream_;
        int vec_dim_, n_data_item_;
        int n_cache; //应该比top-k大
        double read_disk_time_;
        TimeRecord record_;

        DiskIndexBruteForce() {}

        DiskIndexBruteForce(char *index_path, VectorMatrix &user) {
            this->index_stream_ = std::ifstream(index_path, std::ios::binary | std::ios::in);
            this->user_ = user;

            if (!this->index_stream_) {
                std::printf("error in writing index\n");
            }
            this->index_stream_.read((char *) &this->n_data_item_, sizeof(int));
            this->vec_dim_ = user.vec_dim_;

            this->n_cache = std::min(user_.n_vector_, 10000);
        }

        void ResetTimeCalc(){
            read_disk_time_ = 0;
        }

        ~DiskIndexBruteForce() {}

        std::vector<std::vector<RankElement>> Retrieval(VectorMatrix &query_item, int topk) {
            ResetTimeCalc();
            if (topk > this->n_cache || this->n_cache > user_.n_vector_) {
                printf("not support the number, program exit\n");
                exit(-1);
            }

            std::vector<DistancePair> distance_cache(n_cache * n_data_item_);
            int n_query_item = query_item.n_vector_;
            int n_user = user_.n_vector_;
            int n_batch = n_user / n_cache;
            int n_remain = n_user % n_cache;

            std::vector<std::vector<RankElement>> query_heap_l(n_query_item, std::vector<RankElement>(topk));

            record_.reset();
            this->index_stream_.read((char *) distance_cache.data(), n_cache * n_data_item_ * sizeof(DistancePair));
            read_disk_time_ += record_.get_elapsed_time_second();

            for (int cacheID = 0; cacheID < topk; cacheID++) {
                int userID = cacheID;
                for (int qID = 0; qID < n_query_item; qID++) {
                    double *query_item_vec = query_item.getVector(qID);
                    int tmp_rank = getRank(query_item_vec, userID, cacheID, distance_cache);
                    query_heap_l[qID][cacheID].index_ = userID;
                    query_heap_l[qID][cacheID].rank_ = tmp_rank;
                }
            }
            for (int qID = 0; qID < n_query_item; qID++) {
                std::make_heap(query_heap_l[qID].begin(), query_heap_l[qID].end(), std::less<RankElement>());
            }

            for (int cacheID = topk; cacheID < n_cache; cacheID++) {
                int userID = cacheID;
                for (int qID = 0; qID < n_query_item; qID++) {
                    std::vector<RankElement> &tmp_heap = query_heap_l[qID];
                    RankElement min_heap_ele = tmp_heap.front();
                    double *query_item_vec = query_item.getVector(qID);
                    int tmp_rank = getRank(query_item_vec, userID, cacheID, distance_cache);
                    if (min_heap_ele.rank_ > tmp_rank) {
                        std::pop_heap(tmp_heap.begin(), tmp_heap.end(), std::less<RankElement>());
                        tmp_heap.pop_back();
                        tmp_heap.emplace_back(userID, tmp_rank);
                        std::push_heap(tmp_heap.begin(), tmp_heap.end(), std::less<RankElement>());
                    }
                }
            }

            for (int bth_idx = 1; bth_idx < n_batch; bth_idx++) {
                record_.reset();
                this->index_stream_.read((char *) distance_cache.data(), n_cache * n_data_item_ * sizeof(DistancePair));
                read_disk_time_ += record_.get_elapsed_time_second();

                for (int cacheID = 0; cacheID < n_cache; cacheID++) {
                    int userID = bth_idx * n_cache + cacheID;
                    for (int qID = 0; qID < n_query_item; qID++) {
                        std::vector<RankElement> &tmp_heap = query_heap_l[qID];
                        RankElement min_heap_ele = tmp_heap.front();
                        double *query_item_vec = query_item.getVector(qID);
                        int tmp_rank = getRank(query_item_vec, userID, cacheID, distance_cache);
                        if (min_heap_ele.rank_ > tmp_rank) {
                            std::pop_heap(tmp_heap.begin(), tmp_heap.end(), std::less<RankElement>());
                            tmp_heap.pop_back();
                            tmp_heap.emplace_back(userID, tmp_rank);
                            std::push_heap(tmp_heap.begin(), tmp_heap.end(),
                                           std::less<RankElement>());
                        }
                    }

                }

            }

            if (n_remain != 0) {
                record_.reset();
                this->index_stream_.read((char *) distance_cache.data(),
                                         n_remain * n_data_item_ * sizeof(DistancePair));
                read_disk_time_+= record_.get_elapsed_time_second();

                for (int cacheID = 0; cacheID < n_remain; cacheID++) {
                    int userID = n_batch * n_cache + cacheID;
                    for (int qID = 0; qID < n_query_item; qID++) {
                        std::vector<RankElement> &tmp_heap = query_heap_l[qID];
                        RankElement min_heap_ele = tmp_heap.front();
                        double *query_item_vec = query_item.getVector(qID);
                        int tmp_rank = getRank(query_item_vec, userID, cacheID, distance_cache);
                        if (min_heap_ele.rank_ > tmp_rank) {
                            std::pop_heap(tmp_heap.begin(), tmp_heap.end(), std::less<RankElement>());
                            tmp_heap.pop_back();
                            tmp_heap.emplace_back(userID, tmp_rank);
                            std::push_heap(tmp_heap.begin(), tmp_heap.end(),
                                           std::less<RankElement>());
                        }
                    }
                }
            }

            this->index_stream_.close();

            for (int qID = 0; qID < n_query_item; qID++) {
                std::sort(query_heap_l[qID].begin(), query_heap_l[qID].end(), std::less<RankElement>());
            }

            return query_heap_l;
        }

        int getRank(double *query_item_vec, int userID, int cacheID, std::vector<DistancePair> &distance_cache) const {
            double *user_vec = user_.getVector(userID);
            double query_dist = InnerProduct(query_item_vec, user_vec, vec_dim_);
            DistancePair *dpPtr = distance_cache.data() + cacheID * n_data_item_;

            int low = 0;
            int high = n_data_item_;
            int rank = -1;
            //descending
            while (low <= high) {
                int mid = (low + high) / 2;
                if (mid == 0) {
                    if (query_dist >= dpPtr[mid].dist_) {
                        rank = 1;
                        break;
                    } else if (query_dist < dpPtr[mid].dist_ && query_dist > dpPtr[mid + 1].dist_) {
                        rank = 2;
                        break;
                    } else if (query_dist < dpPtr[mid].dist_ && query_dist <= dpPtr[mid + 1].dist_) {
                        low = mid + 1;
                    }
                } else if (0 < mid && mid < n_data_item_ - 1) {
                    if (query_dist > dpPtr[mid].dist_) {
                        high = mid - 1;
                    } else if (query_dist <= dpPtr[mid].dist_ &&
                               query_dist > dpPtr[mid + 1].dist_) {
                        rank = mid + 2;
                        break;
                    } else if (query_dist <= dpPtr[mid].dist_ &&
                               query_dist <= dpPtr[mid + 1].dist_) {
                        low = mid + 1;
                    }
                } else if (mid == n_data_item_ - 1) {
                    if (query_dist <= dpPtr[mid].dist_) {
                        rank = n_data_item_ + 1;
                        break;
                    } else if (query_dist <= dpPtr[mid - 1].dist_ && query_dist > dpPtr[mid].dist_) {
                        rank = n_data_item_;
                        break;
                    } else if (query_dist > dpPtr[mid - 1].dist_ && query_dist > dpPtr[mid].dist_) {
                        high = mid - 1;
                    }
                }
            }
            if (rank <= 0) {
                printf("bug\n");
            }
            return rank;
        }

    };

}

#endif //REVERSE_KRANKS_DISKINDEXBRUTEFORCE_HPP
