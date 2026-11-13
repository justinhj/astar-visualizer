#pragma once

#include <cstdint>
#include <utility>
#include <vector>

enum class GraphLayout {
    RadialPulse = 0,
    OrganicConstellation = 1,
    WarpedMesh = 2,
};

struct GraphEdge {
    int to = -1;
    float euclidean_dist = 0.0f;
    float weight_factor = 1.0f;
    float cost = 0.0f;
};

struct GraphNode {
    int id = -1;
    float x = 0.5f;           // Normalized [0, 1]
    float y = 0.5f;           // Normalized [0, 1]
    float angle = 0.0f;       // Polar angle from canvas center in [-pi, pi]
    float radius_norm = 0.0f; // Normalized distance from center
    std::vector<GraphEdge> edges;
};

struct EdgeRecord {
    int u = -1;
    int v = -1;
    float euclidean_dist = 0.0f;
    float weight_factor = 1.0f;
    float cost = 0.0f;
};

class WeightedGraph {
   public:
    void Generate(int num_nodes, GraphLayout layout, uint32_t seed);

    int NodeCount() const { return static_cast<int>(nodes_.size()); }
    int EdgeCount() const { return static_cast<int>(edges_.size()); }

    const std::vector<GraphNode>& nodes() const { return nodes_; }
    const std::vector<EdgeRecord>& edges() const { return edges_; }

    float EuclideanDistance(int u, int v) const;
    float GetEdgeCost(int u, int v) const;
    float GetEdgeWeightFactor(int u, int v) const;

    std::pair<int, int> PickDefaultStartGoal() const;

   private:
    void BuildRadialPulse(int num_nodes, uint32_t seed);
    void BuildOrganicConstellation(int num_nodes, uint32_t seed);
    void BuildWarpedMesh(int num_nodes, uint32_t seed);
    void ConnectGraph(uint32_t seed);
    void AddUndirectedEdge(int u, int v, float weight_factor);
    bool HasEdge(int u, int v) const;

    std::vector<GraphNode> nodes_;
    std::vector<EdgeRecord> edges_;
};
