#ifndef DOMINANCE_H
#define DOMINANCE_H
#include "blocks/block_visitor.h"
#include "blocks/basic_blocks.h"
#include "blocks/stmt.h"
#include <map>
#include <vector>
#include <stack>
#include <string>
#include <bitset>
#include <algorithm>

namespace block {
class dominator_analysis {
    public:
        dominator_analysis(basic_block::cfg_block cfg, bool is_postdom = false);
        basic_block::cfg_block cfg_;
        bool is_postdom_;
        int max_depth;
        unsigned int max_depth_bb_id;
        std::vector<int> &get_postorder_bb_map();
        std::vector<int> &get_postorder();
        std::vector<int> &get_preorder_bb_map();
        std::vector<int> &get_preorder();
        std::vector<int> &get_idom();
        std::map<int, std::vector<int>> &get_idom_map();
        std::vector<int> &get_postorder_idom_map();
        int get_idom(int bb_id);
        std::vector<int> get_idom_map(int bb_id);
        int get_postorder_idom_map(int idom_id);
        bool dominates(int bb1_id, int bb2_id);
        bool is_reachable_from_entry(int bb_id);
        void analyze();

    private:
        std::vector<int> idom;
        std::map<int, std::vector<int>> idom_map;
        std::vector<int> postorder_idom;
        std::vector<int> postorder;
        std::vector<int> postorder_bb_map;
        std::vector<int> preorder;
        std::vector<int> preorder_bb_map;
        void reverse_cfg();
        void postorder_idom_helper(std::vector<bool> &visited, int id);
        void postorder_dfs_helper(std::vector<bool> &visited_bbs, int id, int depth);
        void postorder_dfs(bool reverse_cfg);
        void preorder_dfs_helper(std::vector<bool> &visited_bbs, int id);
        void preorder_dfs(bool reverse_cfg);
        int intersect(int bb1_id, int bb2_id);
};

void dump(dominator_analysis &dom);
} // namespace block

#endif
