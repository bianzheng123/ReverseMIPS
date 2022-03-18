//
// Created by BianZheng on 2022/3/18.
//

#ifndef REVERSE_KRANKS_INTERVALSEARCH_HPP
#define REVERSE_KRANKS_INTERVALSEARCH_HPP

#include <cassert>
#include <vector>
#include <algorithm>
#include "struct/RankBoundElement.hpp"
#include "spdlog/spdlog.h"

namespace ReverseMIPS {

    class IntervalSearch {

        int n_interval_, n_user_, n_data_item_;
        // n_user * n_interval, the last element of an interval column must be n_data_item
        std::unique_ptr<int[]> interval_table_;
        // n_user, stores the distance of interval for each user
        std::unique_ptr<double[]> interval_dist_l_;
        // n_user, bound for column, first is lower bound, second is upper bound
        std::unique_ptr<std::pair<double, double>[]> user_ip_bound_l_;
    public:

        inline IntervalSearch() = default;

        inline IntervalSearch(const int &n_interval, const int &n_user, const int n_data_item) {
            this->n_interval_ = n_interval;
            this->n_user_ = n_user;
            this->n_data_item_ = n_data_item;

            interval_table_ = std::make_unique<int[]>(n_user_ * n_interval_);
            std::memset(interval_table_.get(), 0, n_user_ * n_interval_ * sizeof(int));

            interval_dist_l_ = std::make_unique<double[]>(n_user_);
            user_ip_bound_l_ = std::make_unique<std::pair<double, double>[]>(n_user_);
            spdlog::info("interval bound: n_interval {}", n_interval);
        }

        void
        LoopPreprocess(const std::pair<double, double> &bound_pair, const double *distance_ptr, const int &userID) {

            double lb = bound_pair.first;
            double ub = bound_pair.second;
            user_ip_bound_l_[userID] = std::make_pair(lb, ub);
            double interval_distance = (ub - lb) / n_interval_;
            interval_dist_l_[userID] = interval_distance;

            int *interval_ptr = interval_table_.get() + userID * n_interval_;
            for (int itemID = 0; itemID < n_data_item_; itemID++) {
                double ip = distance_ptr[itemID];
                int itv_idx = std::floor((ub - ip) / interval_distance);
                assert(0 <= itv_idx && itv_idx < n_interval_);
                interval_ptr[itv_idx]++;
            }
            for (int intervalID = 1; intervalID < n_interval_; intervalID++) {
                interval_ptr[intervalID] += interval_ptr[intervalID - 1];
            }
            assert(interval_ptr[n_interval_ - 1] == n_data_item_);

        }

        //convert ip_bound to rank_bound
        void Query(const std::vector<std::pair<double, double>> &ip_bound_l, const int &topk,
                   const std::vector<bool> &prune_l, std::vector<std::pair<int, int>> &rank_bound_l) {

            assert(prune_l.size() == n_user_);
            assert(rank_bound_l.size() == n_user_);
            assert(ip_bound_l.size() == n_user_);

            for (int userID = 0; userID < n_user_; userID++) {
                if (prune_l[userID]) {
                    continue;
                }

                std::pair<double, double> user_ip_bound = user_ip_bound_l_[userID];
                std::pair<double, double> ip_bound = ip_bound_l[userID];
                assert(ip_bound.first <= ip_bound.second);

                double itv_dist = interval_dist_l_[userID];
                //for interval id, the higher rank value means the lower queryiP
                long long l_lb = std::ceil((user_ip_bound.second - ip_bound.first) / itv_dist);
                long long l_ub = (long long) std::floor((user_ip_bound.second - ip_bound.second) / itv_dist) - 1;

                int itv_lb_idx = (int) (l_lb % 1000000000);
                int itv_ub_idx = (int) (l_ub % 1000000000);

                if (itv_ub_idx <= -1) {
                    itv_ub_idx = -1;
                } else if (itv_ub_idx >= n_interval_ - 1) {
                    itv_ub_idx = n_interval_ - 1;
                }
                if (itv_lb_idx <= -1) {
                    itv_lb_idx = -1;
                } else if (itv_lb_idx >= n_interval_ - 1) {
                    itv_lb_idx = n_interval_ - 1;
                }
                assert(itv_ub_idx <= itv_lb_idx);
                int *rank_ptr = interval_table_.get() + userID * n_interval_;
                int rank_lb = itv_lb_idx == -1 ? 0 : rank_ptr[itv_lb_idx];
                int rank_ub = itv_ub_idx == -1 ? 0 : rank_ptr[itv_ub_idx];

                rank_bound_l[userID] = std::make_pair(rank_lb, rank_ub);
            }
        }

    };
}
#endif //REVERSE_KRANKS_INTERVALSEARCH_HPP
