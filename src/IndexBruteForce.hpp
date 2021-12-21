#pragma once

#include "util/SpaceInnerProduct.hpp"
#include "struct/RankElement.hpp"
#include "struct/VectorMatrix.hpp"
#include "util/TimeMemory.hpp"
#include "struct/DistancePair.hpp"
#include <vector>
#include <cassert>
#include <queue>

namespace ReverseMIPS {
    class Index {
    public:
        std::vector<DistancePair> dist_arr_;
        //n_user_ is row, n_data_item_ is column
        int n_user_, n_data_item_;

        Index() {
            this->n_data_item_ = 0;
            this->n_user_ = 0;
        }

        void init(std::vector<DistancePair> dist_arr, int n_data_item, int n_user) {
            this->dist_arr_ = dist_arr;
            this->n_data_item_ = n_data_item;
            this->n_user_ = n_user;
        }

        DistancePair *getUserDistPtr(int userID) {
            assert(userID >= n_user_);
            return dist_arr_.data() + userID * n_data_item_;
        }
    };

    class IndexBruteForce {
    public:
        VectorMatrix data_item_, user_;
        Index index_;
        int vec_dim_;
        int preprocess_report_every_ = 100;
        int retrieval_report_every_ = 100;

        IndexBruteForce() {}

        IndexBruteForce(VectorMatrix
                        &data_item,
                        VectorMatrix &user
        ) {
            this->data_item_ = data_item;
            this->user_ = user;
            this->vec_dim_ = user.vec_dim_;
        }

        ~IndexBruteForce() {}

        void Preprocess() {
            int n_data_item = data_item_.n_vector_;
            int n_user = user_.n_vector_;
            std::vector<DistancePair> preprocess_matrix(n_user * n_data_item);

            TimeRecord single_query_record;
            for (int userID = 0; userID < n_user; userID++) {

                for (int itemID = 0; itemID < n_data_item; itemID++) {
                    float query_dist = InnerProduct(data_item_.getVector(itemID), user_.getVector(userID), vec_dim_);
                    preprocess_matrix[userID * n_data_item + itemID] = DistancePair(query_dist, itemID);
                }

                std::make_heap(preprocess_matrix.begin() + userID * n_data_item,
                               preprocess_matrix.begin() + (userID + 1) * n_data_item, std::greater<DistancePair>());
                std::sort_heap(preprocess_matrix.begin() + userID * n_data_item,
                               preprocess_matrix.begin() + (userID + 1) * n_data_item, std::greater<DistancePair>());

                if (userID % preprocess_report_every_ == 0) {
                    std::cout << "preprocessed " << userID / (0.01 * n_user) << " %, "
                              << 1e-6 * single_query_record.get_elapsed_time_micro() << " s/iter" << " Mem: "
                              << get_current_RSS() / 1000000 << " Mb \n";
                    single_query_record.reset();
                }

            }

            index_.init(preprocess_matrix, n_user, n_data_item);
        }

        std::vector<std::vector<RankElement>> Retrieval(VectorMatrix &query_item, int topk) {
            int n_query_item = query_item.n_vector_;
            int n_user = user_.n_vector_;

            std::vector<std::vector<RankElement>> results(n_query_item, std::vector<RankElement>());

            TimeRecord single_query_record;
            for (int qID = 0; qID < n_query_item; qID++) {
                float *query_item_vec = query_item.getVector(qID);
                std::vector<RankElement> &minHeap = results[qID];
                minHeap.resize(topk);

                for (int userID = 0; userID < topk; userID++) {
                    int tmp_rank = getRank(query_item_vec, userID);
                    RankElement rankElement(userID, tmp_rank);
                    minHeap[userID] = rankElement;
                }

                std::make_heap(minHeap.begin(), minHeap.end(), std::less<RankElement>());
                RankElement minHeapEle = minHeap.front();
                for (int userID = topk; userID < n_user; userID++) {
                    int tmpRank = getRank(query_item_vec, userID);
                    RankElement rankElement(userID, tmpRank);
                    if (minHeapEle.rank_ > rankElement.rank_) {
                        std::pop_heap(minHeap.begin(), minHeap.end(), std::less<RankElement>());
                        minHeap.pop_back();
                        minHeap.push_back(rankElement);
                        std::push_heap(minHeap.begin(), minHeap.end(), std::less<RankElement>());
                        minHeapEle = minHeap.front();
                    }
                }
                std::make_heap(minHeap.begin(), minHeap.end(), std::less<RankElement>());
                std::sort_heap(minHeap.begin(), minHeap.end(), std::less<RankElement>());

                if (qID % retrieval_report_every_ == 0) {
                    std::cout << "retrieval " << qID / (0.01 * n_query_item) << " %, "
                              << 1e-6 * single_query_record.get_elapsed_time_micro() << " s/iter" << " Mem: "
                              << get_current_RSS() / 1000000 << " Mb \n";
                    single_query_record.reset();
                }
            }

            return results;
        }

        int getRank(float *query_item_vec, int userID) {
            float *user_vec = user_.getVector(userID);
            float query_dist = InnerProduct(query_item_vec, user_vec, vec_dim_);
            int n_data_item = data_item_.n_vector_;
            DistancePair *dpPtr = index_.getUserDistPtr(userID);

            int low = 0;
            int high = n_data_item;
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
                } else if (0 < mid && mid < n_data_item - 1) {
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
                } else if (mid == n_data_item - 1) {
                    if (query_dist <= dpPtr[mid].dist_) {
                        rank = n_data_item + 1;
                        break;
                    } else if (query_dist <= dpPtr[mid - 1].dist_ && query_dist > dpPtr[mid].dist_) {
                        rank = n_data_item;
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
