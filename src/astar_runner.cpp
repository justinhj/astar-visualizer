#include "astar_runner.h"

#include <algorithm>
#include <functional>

const WeightedGraph* GraphSearchState::s_Graph = nullptr;
AStarRunner* GraphSearchState::s_Runner = nullptr;

float GraphSearchState::GoalDistanceEstimate(GraphSearchState& nodeGoal) {
    return s_Graph->EuclideanDistance(node_id, nodeGoal.node_id);
}

bool GraphSearchState::IsGoal(GraphSearchState& nodeGoal) {
    return node_id == nodeGoal.node_id;
}

bool GraphSearchState::GetSuccessors(AStarSearch<GraphSearchState>* astarsearch,
                                     GraphSearchState* parent_node) {
    const auto& edges = s_Graph->nodes()[node_id].edges;
    int parent_id = parent_node ? parent_node->node_id : -1;

    std::vector<int> relaxed_neighbors;
    relaxed_neighbors.reserve(edges.size());

    for (const auto& edge : edges) {
        if (edge.to == parent_id) continue;
        GraphSearchState succ(edge.to);
        if (!astarsearch->AddSuccessor(succ)) {
            return false;
        }
        relaxed_neighbors.push_back(edge.to);
    }

    if (s_Runner) {
        s_Runner->OnNodeExpanded(node_id, relaxed_neighbors);
    }
    return true;
}

float GraphSearchState::GetCost(GraphSearchState& successor) {
    return s_Graph->GetEdgeCost(node_id, successor.node_id);
}

bool GraphSearchState::IsSameState(GraphSearchState& rhs) {
    return node_id == rhs.node_id;
}

size_t GraphSearchState::Hash() {
    return std::hash<int>{}(node_id);
}

AStarRunner::AStarRunner() = default;

AStarRunner::~AStarRunner() {
    ClearSearchInstance();
}

void AStarRunner::ClearSearchInstance() {
    if (search_) {
        if (state_ == RunnerState::Searching) {
            // Cancel and step once so stlastar frees any open/closed nodes cleanly
            search_->CancelSearch();
            search_->SearchStep();
        }
        search_.reset();
    }
}

void AStarRunner::EnsureMemoryFreed() {
    if (search_) {
        search_->EnsureMemoryFreed();
    }
}

void AStarRunner::Reset(const WeightedGraph* graph, int start_id, int goal_id) {
    ClearSearchInstance();

    graph_ = graph;
    start_id_ = start_id;
    goal_id_ = goal_id;
    step_count_ = 0;
    open_count_ = 0;
    closed_count_ = 0;
    current_expanding_ = -1;
    solution_cost_ = 0.0f;
    solution_path_.clear();
    active_neighbors_.clear();

    if (!graph_ || graph_->NodeCount() == 0) {
        state_ = RunnerState::Idle;
        node_vis_.clear();
        return;
    }

    const int n = graph_->NodeCount();
    start_id_ = std::clamp(start_id_, 0, n - 1);
    goal_id_ = std::clamp(goal_id_, 0, n - 1);

    node_vis_.assign(n, NodeVisInfo{});
    for (int i = 0; i < n; ++i) {
        node_vis_[i].pulse_phase = static_cast<float>(i) * 0.47f;
    }

    GraphSearchState::s_Graph = graph_;
    GraphSearchState::s_Runner = this;

    search_ = std::make_unique<AStarSearch<GraphSearchState>>(4096);
    GraphSearchState s(start_id_);
    GraphSearchState g(goal_id_);
    search_->SetStartAndGoalStates(s, g);

    state_ = RunnerState::Searching;
    SyncListsFromSearch();
}

void AStarRunner::OnNodeExpanded(int node_id, const std::vector<int>& neighbors) {
    current_expanding_ = node_id;
    active_neighbors_ = neighbors;

    if (node_vis_[node_id].step_closed < 0) {
        node_vis_[node_id].step_closed = step_count_;
    }

    float parent_g = node_vis_[node_id].g;
    for (int nb : neighbors) {
        float cand_g = parent_g + graph_->GetEdgeCost(node_id, nb);
        if (node_vis_[nb].status == NodeStatus::Unvisited || cand_g < node_vis_[nb].g) {
            node_vis_[nb].parent_id = node_id;
            node_vis_[nb].g = cand_g;
        }
    }
}

void AStarRunner::SyncListsFromSearch() {
    if (!search_) return;

    // Reset open/closed markers while preserving parent_id & discovery history
    for (auto& info : node_vis_) {
        if (info.status == NodeStatus::Open ||
            info.status == NodeStatus::Closed ||
            info.status == NodeStatus::CurrentExpanding) {
            info.status = NodeStatus::Unvisited;
        }
    }

    open_count_ = 0;
    closed_count_ = 0;

    float f = 0.0f, g = 0.0f, h = 0.0f;
    for (GraphSearchState* st = search_->GetClosedListStart(f, g, h);
         st != nullptr;
         st = search_->GetClosedListNext(f, g, h)) {
        int id = st->node_id;
        if (id >= 0 && id < static_cast<int>(node_vis_.size())) {
            node_vis_[id].status = NodeStatus::Closed;
            node_vis_[id].f = f;
            node_vis_[id].g = g;
            node_vis_[id].h = h;
            if (node_vis_[id].step_discovered < 0) node_vis_[id].step_discovered = step_count_;
            if (node_vis_[id].step_closed < 0) node_vis_[id].step_closed = step_count_;
            ++closed_count_;
        }
    }

    for (GraphSearchState* st = search_->GetOpenListStart(f, g, h);
         st != nullptr;
         st = search_->GetOpenListNext(f, g, h)) {
        int id = st->node_id;
        if (id >= 0 && id < static_cast<int>(node_vis_.size())) {
            node_vis_[id].status = NodeStatus::Open;
            node_vis_[id].f = f;
            node_vis_[id].g = g;
            node_vis_[id].h = h;
            if (node_vis_[id].step_discovered < 0) node_vis_[id].step_discovered = step_count_;
            ++open_count_;
        }
    }

    if (current_expanding_ >= 0 &&
        current_expanding_ < static_cast<int>(node_vis_.size())) {
        node_vis_[current_expanding_].status = NodeStatus::CurrentExpanding;
    }
}

bool AStarRunner::Step() {
    if (state_ != RunnerState::Searching || !search_) {
        return false;
    }

    GraphSearchState::s_Graph = graph_;
    GraphSearchState::s_Runner = this;

    current_expanding_ = -1;
    active_neighbors_.clear();

    unsigned int res = search_->SearchStep();
    step_count_ = search_->GetStepCount();

    if (res == AStarSearch<GraphSearchState>::SEARCH_STATE_SEARCHING) {
        SyncListsFromSearch();
        return true;
    }

    if (res == AStarSearch<GraphSearchState>::SEARCH_STATE_SUCCEEDED) {
        // Note: stlastar.h frees unused nodes right before returning SUCCEEDED,
        // so we preserve our last Open/Closed snapshot and overlay the final solution path.
        solution_cost_ = search_->GetSolutionCost();
        solution_path_.clear();

        int prev_id = -1;
        for (GraphSearchState* node = search_->GetSolutionStart();
             node != nullptr;
             node = search_->GetSolutionNext()) {
            int id = node->node_id;
            solution_path_.push_back(id);
            if (id >= 0 && id < static_cast<int>(node_vis_.size())) {
                node_vis_[id].status = NodeStatus::OnPath;
                if (prev_id >= 0) {
                    node_vis_[id].parent_id = prev_id;
                }
            }
            prev_id = id;
        }

        // Convert any remaining CurrentExpanding marker to Closed unless it's on path
        for (auto& info : node_vis_) {
            if (info.status == NodeStatus::CurrentExpanding) {
                info.status = NodeStatus::Closed;
            }
        }

        search_->FreeSolutionNodes();
        current_expanding_ = -1;
        active_neighbors_.clear();
        state_ = RunnerState::Succeeded;
        return false;
    }

    // Failed or Out of Memory
    state_ = RunnerState::Failed;
    current_expanding_ = -1;
    active_neighbors_.clear();
    return false;
}
