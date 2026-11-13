#pragma once

#include "graph.h"
#include "stlastar.h"

#include <memory>
#include <vector>

class AStarRunner;

struct GraphSearchState {
    int node_id = -1;

    static const WeightedGraph* s_Graph;
    static AStarRunner* s_Runner;

    GraphSearchState() = default;
    explicit GraphSearchState(int id) : node_id(id) {}

    float GoalDistanceEstimate(GraphSearchState& nodeGoal);
    bool IsGoal(GraphSearchState& nodeGoal);
    bool GetSuccessors(AStarSearch<GraphSearchState>* astarsearch, GraphSearchState* parent_node);
    float GetCost(GraphSearchState& successor);
    bool IsSameState(GraphSearchState& rhs);
    size_t Hash();
};

enum class NodeStatus {
    Unvisited,
    Open,
    Closed,
    CurrentExpanding,
    OnPath,
};

enum class RunnerState {
    Idle,
    Searching,
    Succeeded,
    Failed,
};

struct NodeVisInfo {
    NodeStatus status = NodeStatus::Unvisited;
    float g = 0.0f;
    float h = 0.0f;
    float f = 0.0f;
    int parent_id = -1;
    int step_discovered = -1;
    int step_closed = -1;
    float pulse_phase = 0.0f;
};

class AStarRunner {
   public:
    AStarRunner();
    ~AStarRunner();

    void Reset(const WeightedGraph* graph, int start_id, int goal_id);
    bool Step();

    RunnerState state() const { return state_; }
    int start_id() const { return start_id_; }
    int goal_id() const { return goal_id_; }
    int step_count() const { return step_count_; }
    int open_count() const { return open_count_; }
    int closed_count() const { return closed_count_; }
    int current_expanding() const { return current_expanding_; }
    float solution_cost() const { return solution_cost_; }

    const std::vector<NodeVisInfo>& node_vis() const { return node_vis_; }
    const std::vector<int>& solution_path() const { return solution_path_; }
    const std::vector<int>& active_neighbors() const { return active_neighbors_; }

    // Called from GraphSearchState::GetSuccessors during SearchStep()
    void OnNodeExpanded(int node_id, const std::vector<int>& neighbors);

    // Exposes memory check for tests
    void EnsureMemoryFreed();

   private:
    void SyncListsFromSearch();
    void ClearSearchInstance();

    const WeightedGraph* graph_ = nullptr;
    int start_id_ = 0;
    int goal_id_ = 0;
    int step_count_ = 0;
    int open_count_ = 0;
    int closed_count_ = 0;
    int current_expanding_ = -1;
    float solution_cost_ = 0.0f;
    RunnerState state_ = RunnerState::Idle;

    std::unique_ptr<AStarSearch<GraphSearchState>> search_;
    std::vector<NodeVisInfo> node_vis_;
    std::vector<int> solution_path_;
    std::vector<int> active_neighbors_;
};
