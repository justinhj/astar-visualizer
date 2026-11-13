#include "astar_runner.h"
#include "graph.h"
#include "renderer.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct AppContext {
    WeightedGraph graph;
    AStarRunner runner;
    UiState ui;
    uint32_t seed = 104729;
    int start_id = 0;
    int goal_id = 1;
    float step_accum = 0.0f;

    void RegenerateGraph() {
        graph.Generate(ui.target_node_count, ui.layout, seed);
        auto sg = graph.PickDefaultStartGoal();
        start_id = sg.first;
        goal_id = sg.second;
        runner.Reset(&graph, start_id, goal_id);
        step_accum = 0.0f;
    }

    void RestartSearch() {
        runner.Reset(&graph, start_id, goal_id);
        step_accum = 0.0f;
    }
};

void UpdateSizeSlider(AppContext& app, const Renderer& renderer, float mx) {
    const RectF& track = renderer.hitboxes().size_slider_track;
    float t = std::clamp((mx - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
    int new_count = static_cast<int>(std::round(12.0f + t * (220.0f - 12.0f)));
    if (new_count != app.ui.target_node_count) {
        app.ui.target_node_count = new_count;
        app.RegenerateGraph();
    }
}

void UpdateSpeedSlider(AppContext& app, const Renderer& renderer, float mx) {
    const RectF& track = renderer.hitboxes().speed_slider_track;
    float t = std::clamp((mx - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
    app.ui.steps_per_sec = std::round(1.0f + t * 39.0f);
}

int RunHeadlessDemo(const std::string& out_path, int nodes, int layout_idx, int max_steps) {
    AppContext app;
    app.ui.target_node_count = std::clamp(nodes, 12, 220);
    app.ui.layout = static_cast<GraphLayout>(std::clamp(layout_idx, 0, 2));
    app.RegenerateGraph();

    if (max_steps < 0) {
        while (app.runner.Step()) {
        }
    } else {
        for (int i = 0; i < max_steps && app.runner.Step(); ++i) {
        }
    }

    Renderer renderer;
    renderer.Render(1440, 900, 1.25f, app.graph, app.runner, app.ui);
    if (!renderer.SavePNG(out_path)) {
        std::fprintf(stderr, "Failed to save PNG to %s\n", out_path.c_str());
        return 1;
    }
    std::printf("Saved headless preview to %s (nodes=%d, steps=%d, state=%d)\n",
                out_path.c_str(), app.graph.NodeCount(), app.runner.step_count(),
                static_cast<int>(app.runner.state()));
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string headless_path;
    int cli_nodes = 64;
    int cli_layout = 0;
    int cli_steps = -1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless-demo" && i + 1 < argc) {
            headless_path = argv[++i];
        } else if (arg == "--nodes" && i + 1 < argc) {
            cli_nodes = std::atoi(argv[++i]);
        } else if (arg == "--layout" && i + 1 < argc) {
            cli_layout = std::atoi(argv[++i]);
        } else if (arg == "--steps" && i + 1 < argc) {
            cli_steps = std::atoi(argv[++i]);
        }
    }

    if (!headless_path.empty()) {
        return RunHeadlessDemo(headless_path, cli_nodes, cli_layout, cli_steps);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int win_w = 1440;
    int win_h = 900;
    SDL_Window* window = SDL_CreateWindow(
        "ASTAR // Weighted Graph Pathfinder", win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    if (!sdl_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
    if (!texture) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(sdl_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    AppContext app;
    app.ui.target_node_count = std::clamp(cli_nodes, 12, 220);
    app.ui.layout = static_cast<GraphLayout>(std::clamp(cli_layout, 0, 2));
    app.RegenerateGraph();

    Renderer renderer;
    auto t_start = std::chrono::steady_clock::now();
    auto t_prev = t_start;
    bool running = true;

    while (running) {
        auto t_now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        float elapsed = std::chrono::duration<float>(t_now - t_start).count();
        t_prev = t_now;
        dt = std::min(dt, 0.1f);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
                int nw = std::max(320, ev.window.data1);
                int nh = std::max(240, ev.window.data2);
                if (nw != win_w || nh != win_h) {
                    win_w = nw;
                    win_h = nh;
                    SDL_DestroyTexture(texture);
                    texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                float mx = ev.motion.x;
                float my = ev.motion.y;
                if (app.ui.dragging_size_slider) {
                    UpdateSizeSlider(app, renderer, mx);
                } else if (app.ui.dragging_speed_slider) {
                    UpdateSpeedSlider(app, renderer, mx);
                } else {
                    app.ui.hovered_node = renderer.HitTestNode(app.graph, mx, my);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                app.ui.dragging_size_slider = false;
                app.ui.dragging_speed_slider = false;
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                float mx = ev.button.x;
                float my = ev.button.y;
                const auto& hb = renderer.hitboxes();

                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (hb.play_button.Contains(mx, my)) {
                        if (app.runner.state() != RunnerState::Searching) {
                            app.RestartSearch();
                            app.ui.is_playing = true;
                        } else {
                            app.ui.is_playing = !app.ui.is_playing;
                        }
                    } else if (hb.step_button.Contains(mx, my)) {
                        app.ui.is_playing = false;
                        if (app.runner.state() != RunnerState::Searching) {
                            app.RestartSearch();
                        }
                        app.runner.Step();
                    } else if (hb.weights_button.Contains(mx, my)) {
                        app.ui.show_weights = !app.ui.show_weights;
                    } else if (hb.new_graph_button.Contains(mx, my)) {
                        app.seed += 7919u;
                        app.RegenerateGraph();
                    } else if (hb.reset_button.Contains(mx, my)) {
                        app.RestartSearch();
                    } else if (hb.size_slider_track.Contains(mx, my)) {
                        app.ui.dragging_size_slider = true;
                        UpdateSizeSlider(app, renderer, mx);
                    } else if (hb.speed_slider_track.Contains(mx, my)) {
                        app.ui.dragging_speed_slider = true;
                        UpdateSpeedSlider(app, renderer, mx);
                    } else {
                        bool handled_layout = false;
                        for (int i = 0; i < 3; ++i) {
                            if (hb.layout_buttons[i].Contains(mx, my)) {
                                app.ui.layout = static_cast<GraphLayout>(i);
                                app.RegenerateGraph();
                                handled_layout = true;
                                break;
                            }
                        }
                        if (!handled_layout) {
                            int hit = renderer.HitTestNode(app.graph, mx, my);
                            if (hit >= 0) {
                                SDL_Keymod mods = SDL_GetModState();
                                bool set_goal = (mods & SDL_KMOD_SHIFT) != 0;
                                if (set_goal) {
                                    if (hit != app.start_id) app.goal_id = hit;
                                } else {
                                    if (hit != app.goal_id) app.start_id = hit;
                                }
                                app.RestartSearch();
                            }
                        }
                    }
                } else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    int hit = renderer.HitTestNode(app.graph, mx, my);
                    if (hit >= 0 && hit != app.start_id) {
                        app.goal_id = hit;
                        app.RestartSearch();
                    }
                }
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode key = ev.key.key;
                if (key == SDLK_ESCAPE) {
                    running = false;
                } else if (key == SDLK_SPACE) {
                    if (app.runner.state() != RunnerState::Searching) {
                        app.RestartSearch();
                        app.ui.is_playing = true;
                    } else {
                        app.ui.is_playing = !app.ui.is_playing;
                    }
                } else if (key == SDLK_S) {
                    app.ui.is_playing = false;
                    if (app.runner.state() != RunnerState::Searching) {
                        app.RestartSearch();
                    }
                    app.runner.Step();
                } else if (key == SDLK_R) {
                    app.seed += 7919u;
                    app.RegenerateGraph();
                } else if (key == SDLK_C) {
                    app.RestartSearch();
                } else if (key == SDLK_W) {
                    app.ui.show_weights = !app.ui.show_weights;
                } else if (key == SDLK_1) {
                    app.ui.layout = GraphLayout::RadialPulse;
                    app.RegenerateGraph();
                } else if (key == SDLK_2) {
                    app.ui.layout = GraphLayout::OrganicConstellation;
                    app.RegenerateGraph();
                } else if (key == SDLK_3) {
                    app.ui.layout = GraphLayout::WarpedMesh;
                    app.RegenerateGraph();
                }
            }
        }

        if (app.ui.is_playing && app.runner.state() == RunnerState::Searching) {
            app.step_accum += dt * app.ui.steps_per_sec;
            while (app.step_accum >= 1.0f && app.runner.state() == RunnerState::Searching) {
                app.runner.Step();
                app.step_accum -= 1.0f;
            }
        }

        renderer.Render(win_w, win_h, elapsed, app.graph, app.runner, app.ui);

        SDL_UpdateTexture(texture, nullptr, renderer.pixels(), renderer.stride());
        SDL_RenderClear(sdl_renderer);
        SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
