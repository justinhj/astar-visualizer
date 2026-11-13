#ifdef NDEBUG
#undef NDEBUG
#endif
#include "astar_runner.h"
#include "graph.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <queue>
#include <vector>

namespace {

void VerifyConnectedAndWeighted(const WeightedGraph& g) {
    const int n = g.NodeCount();
    assert(n >= 8);
    assert(g.EdgeCount() >= n - 1);

    for (const auto& e : g.edges()) {
        assert(e.u >= 0 && e.u < n);
        assert(e.v >= 0 && e.v < n);
        assert(e.u != e.v);
        assert(e.weight_factor >= 1.0f);
        assert(e.cost >= e.euclidean_dist * 0.999f);
    }

    // BFS connectivity check
    std::vector<bool> vis(n, false);
    std::queue<int> q;
    vis[0] = true;
    q.push(0);
    int seen = 0;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        ++seen;
        for (const auto& e : g.nodes()[u].edges) {
            if (!vis[e.to]) {
                vis[e.to] = true;
                q.push(e.to);
            }
        }
    }
    assert(seen == n);
}

float DijkstraReferenceCost(const WeightedGraph& g, int start, int goal) {
    const int n = g.NodeCount();
    std::vector<float> dist(n, 1e30f);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    dist[start] = 0.0f;
    pq.push({0.0f, start});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;
        if (u == goal) return d;
        for (const auto& e : g.nodes()[u].edges) {
            float nd = d + e.cost;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }
    return 1e30f;
}

}  // namespace

int main() {
    const int sizes[] = {12, 64, 180};
    const GraphLayout layouts[] = {
        GraphLayout::RadialPulse,
        GraphLayout::OrganicConstellation,
        GraphLayout::WarpedMesh,
    };

    for (GraphLayout layout : layouts) {
        for (int sz : sizes) {
            WeightedGraph graph;
            graph.Generate(sz, layout, 12345u + static_cast<uint32_t>(sz));
            VerifyConnectedAndWeighted(graph);

            auto sg = graph.PickDefaultStartGoal();
            AStarRunner runner;
            runner.Reset(&graph, sg.first, sg.second);
            assert(runner.state() == RunnerState::Searching);

            int guard = 0;
            while (runner.Step()) {
                assert(++guard < 5000);
            }
            assert(runner.state() == RunnerState::Succeeded);
            assert(!runner.solution_path().empty());
            assert(runner.solution_path().front() == sg.first);
            assert(runner.solution_path().back() == sg.second);

            float expected = DijkstraReferenceCost(graph, sg.first, sg.second);
            float diff = std::fabs(runner.solution_cost() - expected);
            assert(diff < 0.05f);

            runner.EnsureMemoryFreed();
        }
    }

    // Also verify mid-search cancellation and re-reset
    {
        WeightedGraph graph;
        graph.Generate(68, GraphLayout::RadialPulse, 999u);
        AStarRunner runner;
        runner.Reset(&graph, 0, graph.NodeCount() - 1);
        runner.Step();
        runner.Step();
        runner.Reset(&graph, 1, graph.NodeCount() / 2);
        while (runner.Step()) {
        }
        assert(runner.state() == RunnerState::Succeeded);
        runner.EnsureMemoryFreed();
    }

    std::printf("All astar_tests passed!\n");
    return 0;
}
