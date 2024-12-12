#include "blocks/dominance.h"

// We implement the dominance algorithm from the paper
// "A Simple, Fast Dominance Algorithm" [1]. The following
// implementation [2] has been very useful to replicate the paper on
// buildit and understand the dominance algorithm.
//
// [1] http://www.hipersoft.rice.edu/grads/publications/dom14.pdf
// [2] https://github.com/baziotis/compiler-optimization/tree/master/dominance

namespace block {
// This function reverses the CFG, and it can also handle control
// flow with no exit blocks, basically an infinite loop as well.
// This is necessary to build a postdominator tree.
void dominator_analysis::reverse_cfg() {
    // TODO: Add a check for size, it should be greater than 2.
    if (cfg_.size() == 0)
        assert(0);

    std::shared_ptr<basic_block> virtual_exit_bb = std::make_shared<basic_block>("virtualexit0");
    for (auto bb: cfg_) {
        if (bb->successor.size() == 0) {
            bb->successor.push_back(virtual_exit_bb);
            virtual_exit_bb->predecessor.push_back(bb);
        }
    }

    // if CFG is an infinite loop, it doesn't have a exit block
    // So we find the farthest block from the entry of the loop
    // and consider that as one of the exit blocks.
    if (!virtual_exit_bb->predecessor.size()) {
        // we run a postorder dfs on the CFG to find out the
        // farthest basic block from the entry block.
        postorder_dfs(false);

        auto bb_virtual_backedge = cfg_[max_depth_bb_id];
        bb_virtual_backedge->successor.push_back(virtual_exit_bb);
        virtual_exit_bb->predecessor.push_back(bb_virtual_backedge);
    }

    virtual_exit_bb->id = cfg_.size();
    cfg_.push_back(virtual_exit_bb);

    // Reverse the control flow, now that we have attached a virtual exit block
    for (auto bb: cfg_) {
        basic_block::cfg_block temp_pred = bb->predecessor;
        bb->predecessor.clear();
        bb->predecessor.insert(bb->predecessor.begin(), bb->successor.begin(), bb->successor.end());
        std::reverse(bb->predecessor.begin(), bb->predecessor.end());
        bb->successor.clear();
        bb->successor.insert(bb->successor.begin(), temp_pred.begin(), temp_pred.end());
        std::reverse(bb->successor.begin(), bb->successor.end());
    }
}

// dominator analysis class constructor
// basic_block::cfg_block cfg - Control flow graph whose dominator
//  tree needs to be derived.
// bool is_postdom - true if we need the postdominator tree
//  or else false.
dominator_analysis::dominator_analysis(basic_block::cfg_block cfg, bool is_postdom) : cfg_(cfg), is_postdom_(is_postdom) {
    // check if we want a postdominator tree
    if (is_postdom) {
        reverse_cfg();
    }

    // TODO: Add a check for size, it should be greater than 2.
    idom.clear();
    idom.reserve(cfg_.size());
    idom.assign(cfg_.size(), -1);
    postorder.clear();
    postorder.reserve(cfg_.size());
    postorder_bb_map.clear();
    postorder_bb_map.reserve(cfg_.size());
    postorder_bb_map.assign(cfg_.size(), -1);
    preorder.clear();
    preorder.reserve(cfg_.size());
    preorder_bb_map.clear();
    preorder_bb_map.reserve(cfg_.size());
    preorder_bb_map.assign(cfg_.size(), -1);

    // and call the anaylse function
    analyze();

    // print debug logs
#ifdef DOMINATOR_ANALYSIS_DEBUG    
    dump(*this);
#endif
}

void dominator_analysis::postorder_idom_helper(std::vector<bool> &visited, int id) {
    for (int idom_id: idom_map[id]) {
        if (idom_id != -1 && !visited[idom_id]) {
            visited[idom_id] = true;
            postorder_idom_helper(visited, idom_id);
            postorder_idom.push_back(idom_id);
        }
    }
}

void dominator_analysis::postorder_dfs_helper(std::vector<bool> &visited_bbs, int id, int depth) {
    if (depth > max_depth) {
        max_depth = depth;
        max_depth_bb_id = id;
    }

    for (auto child: cfg_[id]->successor) {
        if (!visited_bbs[child->id]) {
            visited_bbs[child->id] = true;
            postorder_dfs_helper(visited_bbs, child->id, depth + 1);
            postorder.push_back(child->id);
        }
    }
}

// Helper function to populate the postorder array using DFS
void dominator_analysis::postorder_dfs(bool reverse_cfg) {
    int current_depth = 0;
    max_depth = current_depth;

    std::vector<bool> visited_bbs(cfg_.size());
    visited_bbs.assign(visited_bbs.size(), false);
    if (reverse_cfg)
        visited_bbs[cfg_.size() - 1] = true;
    else
        visited_bbs[0] = true;

    if (reverse_cfg) {
        max_depth_bb_id = cfg_.size() - 1;
        postorder_dfs_helper(visited_bbs, cfg_.size() - 1, current_depth + 1);
        postorder.push_back(cfg_.size() - 1);
    }
    else {
        max_depth_bb_id = 0;
        postorder_dfs_helper(visited_bbs, 0, current_depth + 1);
        postorder.push_back(0);
    }
}

void dominator_analysis::preorder_dfs_helper(std::vector<bool> &visited_bbs, int id) {
        for (auto child: cfg_[id]->successor) {
            if (!visited_bbs[child->id]) {
                visited_bbs[child->id] = true;
                preorder.push_back(child->id);
                preorder_dfs_helper(visited_bbs, child->id);
            }
        }
}

// Helper function to populate the preorder array using DFS
void dominator_analysis::preorder_dfs(bool reverse_cfg) {
    std::vector<bool> visited_bbs(cfg_.size());
    visited_bbs.assign(visited_bbs.size(), false);
    if (reverse_cfg)
        visited_bbs[cfg_.size() - 1] = true;
    else
        visited_bbs[0] = true;

    if (reverse_cfg) {
        preorder.push_back(cfg_.size() - 1);
        preorder_dfs_helper(visited_bbs, cfg_.size() - 1);
    }
    else {
        preorder.push_back(0);
        preorder_dfs_helper(visited_bbs, 0);
    }
}

// Returns a map between basic block id and postorder
// The vector is indexed by basic block id, and it returns the
// postorder id of the said basic block.
std::vector<int> &dominator_analysis::get_postorder_bb_map() {
    return postorder_bb_map;
}

// Returns the postorder array
std::vector<int> &dominator_analysis::get_postorder() {
    return postorder;
}

// Returns a map between basic block id and preorder
// The vector is indexed by basic block id, and it returns the
// preorder id of the said basic block.
std::vector<int> &dominator_analysis::get_preorder_bb_map() {
    return preorder_bb_map;
}

// Returns the preorder array
std::vector<int> &dominator_analysis::get_preorder() {
    return preorder;
}

// Returns the immediate dominator array
std::vector<int> &dominator_analysis::get_idom() {
    return idom;
}


// Returns the idom to basic block map
std::map<int, std::vector<int>> &dominator_analysis::get_idom_map() {
    return idom_map;
}

// This returns the postorder visit order of the dominator tree
std::vector<int> &dominator_analysis::get_postorder_idom_map() {
    return postorder_idom;
}

// Returns the immediate dominator of the given basic block
// the idom array stores the idoms of the given basic block, addressed
// by the basic block id.
//
// idom[bb->id] = <idom of bb>
int dominator_analysis::get_idom(int bb_id) {
    if (bb_id < 0 || bb_id >= (int)idom.size()) {
        return -1;
    }

    return idom[bb_id];
}

// Returns all the basic block ids that are dominated by the given
// basic block. This builds a dominator tree using the immediate
// dominators
std::vector<int> dominator_analysis::get_idom_map(int bb_id) {
    if (bb_id < 0 || bb_id >= (int)idom_map.size()) {
        return {};
    }

    return idom_map[bb_id];
}

// This returns the postorder id of the dominator tree
int dominator_analysis::get_postorder_idom_map(int idom_id) {
    if (idom_id < 0 || idom_id >= (int)postorder_idom.size()) {
        return -1;
    }

    return postorder_idom[idom_id];
}

// Simple utility to check if bb1 dominates bb2, returns true if
// it does, and false if not.
bool dominator_analysis::dominates(int bb1_id, int bb2_id) {
    if (bb1_id == 0) {
        return true;
    }

    int pointer = idom[bb2_id];
    while (pointer != 0) {
        if (pointer == bb1_id) {
            return true;
        }
        pointer = idom[pointer];
    }

    return false;
}

// This function checks if the basic block is reachable from entry
// returns true - if basic block 0 domaintes the bb_id or else
// returns false.
bool dominator_analysis::is_reachable_from_entry(int bb_id) {
    return dominates(0, bb_id);
}

int dominator_analysis::intersect(int bb1_id, int bb2_id) {
    while (bb1_id != bb2_id) {
        if (postorder_bb_map[bb1_id] < postorder_bb_map[bb2_id]) {
            bb1_id = idom[bb1_id];
        }
        else {
            bb2_id = idom[bb2_id];
        }
    }

    return bb1_id;
}

// This algorithm iterates over the CFG in a postorder depth first
// approach. For basic blocks with single predecessor, the idom is
// just the predecessor block. But for cases where there are more
// than one predecessor blocks, we walk back based on the postorder
// and find a common block between a pair of two predcessors through
// the intersect call.
void dominator_analysis::analyze() {
    // do a preorder and postorder traversal of the
    // CFG and populate all the vectors/maps
    preorder_dfs(is_postdom_);
    postorder_dfs(is_postdom_);
    for (unsigned int i = 0; i < preorder.size(); i++) {
        preorder_bb_map[preorder[i]] = i;
    }
    for (unsigned int i = 0; i < postorder.size(); i++) {
        postorder_bb_map[postorder[i]] = i;
    }
    if (is_postdom_)
        idom[cfg_.size() - 1] = cfg_.size() - 1;
    else
        idom[0] = 0;

    bool change = false;

    do {
        change = 0;
        for (int i = postorder.size() - 2; i >= 0; i--) {
            int postorder_bb_num = postorder[i];
            std::shared_ptr<basic_block> bb = cfg_[postorder_bb_num];
            int bb_id_idom = bb->predecessor[0]->id;

            for (unsigned int j = 1; j < bb->predecessor.size(); j++) {
                int bb_id_idom_next = bb->predecessor[j]->id;
                if (idom[bb_id_idom_next] != -1) {
                    bb_id_idom = intersect(bb_id_idom, bb_id_idom_next);
                }
            }

            if (idom[postorder_bb_num] != bb_id_idom) {
                idom[postorder_bb_num] = bb_id_idom;
                change = 1;
            }
        }
    } while(change);

    // build a map of dominators for easy traversal.
    for (unsigned int i = 0; i < idom.size(); i++) {
        idom_map[idom[i]].push_back(i);
    }

    // for blocks that don't dominate any other blocks
    // push a -1
    for (unsigned int i = 0; i < idom.size(); i++) {
        if (idom_map[i].empty())
            idom_map[i].push_back(-1);
    }

    // build a postorder visit list of idom_tree
    std::vector<bool> visited_idom_nodes(idom_map.size());
    visited_idom_nodes.assign(visited_idom_nodes.size(), false);
    visited_idom_nodes[0] = true;

    postorder_idom_helper(visited_idom_nodes, 0);
    postorder_idom.push_back(0);
}

void dump(dominator_analysis &dom) {
    std::cerr << "++++++ dominance ++++++ \n";
    if (!dom.is_postdom_) {
        std::cerr << "max depth: " << dom.max_depth << "\n";
        std::cerr << "max depth bb id: " << dom.max_depth_bb_id << "\n";
        std::cerr << "== postorder map ==\n";
        for (int i: dom.get_postorder_bb_map()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== postorder map ==\n";

        std::cerr << "== postorder ==\n";
        for (int i: dom.get_postorder()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== postorder ==\n";

        std::cerr << "== preorder map ==\n";
        for (int i: dom.get_preorder_bb_map()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== preorder map ==\n";

        std::cerr << "== preorder ==\n";
        for (int i: dom.get_preorder()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== preorder ==\n";

        std::cerr << "== idom ==\n";
        std::cerr << "get_idom(int) test: get_idom(0): " << dom.get_idom(0) << "\n";
        std::cerr << "get_idom(int) test: get_idom(-1): " << dom.get_idom(-1) << "\n";

        for (unsigned int i = 0; i < dom.get_idom().size(); i++) {
            std::cerr << i << " : " << dom.get_idom()[i] << "\n";
        }
        std::cerr << "== idom ==\n";

        std::cerr << "== idom map ==\n";
        std::cerr << "get_idom_map(int) test: get_idom_map(0): ";
        for (int i : dom.get_idom_map(0)) std::cerr << i << " ";
        std::cerr << "\n";

        std::cerr << "get_idom_map(int) test: get_idom_map(-1): ";
        for (int i : dom.get_idom_map(-1)) std::cerr << i << " ";
        std::cerr << "\n";

        for (auto children: dom.get_idom_map()) {
            std::cerr << children.first << ": ";
            for (int child: children.second) {
                std::cerr << child << " ";
            }
            std::cerr << "\n";
        }
        std::cerr << "== idom map ==\n";

        std::cerr << "== postorder idom ==\n";
        for (auto idom: dom.get_postorder_idom_map()) {
            std::cerr << idom << "\n";
        }
    	std::cerr << "== postorder idom ==\n";

    }
    else {
        std::cerr << "(postdom) max depth: " << dom.max_depth << "\n";
        std::cerr << "(postdom) max depth bb id: " << dom.max_depth_bb_id << "\n";
        std::cerr << "== (postdom) postorder map ==\n";
        for (int i: dom.get_postorder_bb_map()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== (postdom) postorder map ==\n";

        std::cerr << "== (postdom) postorder ==\n";
        for (int i: dom.get_postorder()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== (postdom) postorder ==\n";

        std::cerr << "== (postdom) preorder map ==\n";
        for (int i: dom.get_preorder_bb_map()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== (postdom) preorder map ==\n";

        std::cerr << "== (postdom) preorder ==\n";
        for (int i: dom.get_preorder()) {
            std::cerr << i << "\n";
        }
        std::cerr << "== (postdom) preorder ==\n";

        std::cerr << "== (postdom) idom ==\n";
        std::cerr << "get_idom(int) test: get_idom(0): " << dom.get_idom(0) << "\n";
        std::cerr << "get_idom(int) test: get_idom(-1): " << dom.get_idom(-1) << "\n";

        for (unsigned int i = 0; i < dom.get_idom().size(); i++) {
            std::cerr << i << " : " << dom.get_idom()[i] << "\n";
        }
        std::cerr << "== (postdom) idom ==\n";

        std::cerr << "== (postdom) idom map ==\n";
        std::cerr << "get_idom_map(int) test: get_idom_map(0): ";
        for (int i : dom.get_idom_map(0)) std::cerr << i << " ";
        std::cerr << "\n";

        std::cerr << "get_idom_map(int) test: get_idom_map(-1): ";
        for (int i : dom.get_idom_map(-1)) std::cerr << i << " ";
        std::cerr << "\n";

        for (auto children: dom.get_idom_map()) {
            std::cerr << children.first << ": ";
            for (int child: children.second) {
                std::cerr << child << " ";
            }
            std::cerr << "\n";
        }
        std::cerr << "== (postdom) idom map ==\n";

        std::cerr << "== (postdom) postorder idom ==\n";
        for (auto idom: dom.get_postorder_idom_map()) {
            std::cerr << idom << "\n";
        }
        std::cerr << "== (postdom) postorder idom ==\n";
    }
    std::cerr << "++++++ dominance ++++++ \n";
}
} // namespace block
