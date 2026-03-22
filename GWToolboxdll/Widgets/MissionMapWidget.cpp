#include "stdafx.h"

#include <GWCA/Context/GameplayContext.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/NPC.h>
#include <GWCA/GameEntities/Pathing.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Utilities/Hooker.h>

#include <ImGuiAddons.h>
#include <Modules/QuestModule.h>
#include <Widgets/Minimap/Minimap.h>
#include <Widgets/MissionMapWidget.h>
#include <Widgets/WorldMapWidget.h>

namespace {
    bool draw_all_terrain_lines = false;
    bool draw_all_minimap_lines = true;
    bool show_vq_overlay = false; // master toggle for all VQ features on mission map

    // VQ overlay colours — configurable via settings
    Color vq_color_inaccessible = IM_COL32(0, 0, 0, 190);
    Color vq_color_fog_unexplored = IM_COL32(0, 0, 0, 140);
    Color vq_color_border = IM_COL32(200, 220, 255, 160);
    Color vq_color_frontier = IM_COL32(255, 200, 50, 200);
    Color vq_color_compass = IM_COL32(180, 220, 255, 100);
    Color vq_color_enemy_alive = IM_COL32(70, 130, 255, 255);
    Color vq_color_enemy_stale = IM_COL32(255, 180, 50, 180);
    Color vq_color_enemy_outline = IM_COL32(0, 0, 0, 200);

    // Enemy tracking for VQ assistance
    enum class EnemyState { Alive, Stale };
    struct TrackedEnemy {
        GW::GamePos pos;
        GW::Vec2f velocity = {0.0f, 0.0f}; // last known movement direction
        EnemyState state = EnemyState::Alive;
    };
    std::unordered_map<uint32_t, TrackedEnemy> tracked_enemies; // agent_id -> tracked enemy
    GW::Constants::MapID tracked_enemies_map_id = static_cast<GW::Constants::MapID>(0);
    GW::Constants::InstanceType tracked_enemies_instance_type = GW::Constants::InstanceType::Loading;

    constexpr float TWO_PI = 6.2831853f;
    constexpr float COMPASS_RANGE = GW::Constants::Range::Compass;
    constexpr float STALE_CHECK_RANGE = COMPASS_RANGE * 0.9f;

    // Pixel-to-game-unit scale — converts pixel thickness to game units
    float cached_px_to_game = 1.f;

    struct StaticMapGeometry {
        struct GameVertex {
            float x, y, z;
            DWORD color;
        };

        static constexpr int MAX_VERTS = 1 << 20;
        GameVertex verts[MAX_VERTS];
        int vert_count = 0;

        int inaccessible_start = 0, inaccessible_count = 0;
        int border_start = 0, border_count = 0;

        bool Any() const { return inaccessible_count || border_count; }

        GameVertex* Alloc(int count)
        {
            if (vert_count + count > MAX_VERTS) return nullptr;
            GameVertex* ptr = verts + vert_count;
            vert_count += count;
            return ptr;
        }
    } static_map_geo;

    // Exploration tracking (fog of war)
    constexpr float EXPLORE_CELL_SIZE = 250.0f;
    bool* explored_cells = nullptr; // parallel to cached_walkable_grid, same dimensions
    GW::Constants::MapID explored_map_id = static_cast<GW::Constants::MapID>(0);
    GW::Constants::InstanceType explored_instance_type = GW::Constants::InstanceType::Loading;

        struct BorderSegment {
        GW::GamePos p1, p2;
    };

    std::vector<BorderSegment> cached_border_segments;

    bool* cached_walkable_grid = nullptr;
    int cached_walkable_grid_size = 0;

    int cached_grid_x0 = 0, cached_grid_y0 = 0, cached_grid_w = 0, cached_grid_h = 0;
    GW::Constants::MapID border_map_id = static_cast<GW::Constants::MapID>(0);
    float border_cached_zoom = 0.0f; // zoom level used when static geometry was last built
    constexpr float BORDER_CELL_SIZE = 300.0f;

    const D3DMATRIX IDENTITY_MATRIX = {{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}};

    bool GamePosToMissionMapScreenPos(const GW::GamePos& game_map_position, GW::Vec2f& screen_coords);

    struct GameToScreenBasis {
        float ox, oy;
        float ax, ay;
        float bx, by;
        bool valid = false;

        void Rebuild()
        {
            valid = false;

            // Sample at three game positions that are guaranteed to be close together.
            // We use absolute game coords 0,0 for the anchor but measure the basis
            // vectors using a step size large enough to avoid float precision issues
            // at the world->screen transform level, then normalise back to per-unit.
            constexpr float STEP = 1000.f; // large enough for precision, small enough to stay in map

            GW::Vec2f s00, s10, s01;
            if (!GamePosToMissionMapScreenPos({0.f, 0.f, 0}, s00) || !GamePosToMissionMapScreenPos({STEP, 0.f, 0}, s10) || !GamePosToMissionMapScreenPos({0.f, STEP, 0}, s01)) return;

            ox = s00.x;
            oy = s00.y;
            // Divide by step to get per-unit basis vectors
            ax = (s10.x - s00.x) / STEP;
            ay = (s10.y - s00.y) / STEP;
            bx = (s01.x - s00.x) / STEP;
            by = (s01.y - s00.y) / STEP;
            valid = true;
        }

        void Project(float gx, float gy, float& sx, float& sy) const
        {
            sx = ox + gx * ax + gy * bx;
            sy = oy + gx * ay + gy * by;
        }
    } g2s;


    // Returns the flat index into cached_walkable_grid / explored_cells for
    // a given absolute grid coord, or -1 if out of bounds.
    int GetCellIndex(int gx, int gy)
    {
        const int lx = gx - cached_grid_x0;
        const int ly = gy - cached_grid_y0;
        if (lx < 0 || lx >= cached_grid_w || ly < 0 || ly >= cached_grid_h) return -1;
        return ly * cached_grid_w + lx;
    }

    bool IsCellExplored(int gx, int gy)
    {
        if (!explored_cells) return false;
        const int idx = GetCellIndex(gx, gy);
        return idx >= 0 && explored_cells[idx];
    }

    void UpdateExploration(const GW::GamePos& player_pos)
    {
        const auto map_id = GW::Map::GetMapID();
        const auto instance_type = GW::Map::GetInstanceType();
        if (map_id != explored_map_id || instance_type != explored_instance_type) {
            delete[] explored_cells;
            explored_cells = nullptr;
            explored_map_id = map_id;
            explored_instance_type = instance_type;
        }

        if (instance_type != GW::Constants::InstanceType::Explorable) return;

        // Allocate parallel to walkable grid when first entering an explorable area
        if (!explored_cells && cached_walkable_grid_size > 0) {
            explored_cells = new bool[cached_walkable_grid_size](); // zero-init
        }
        if (!explored_cells) return;

        const int range_cells = static_cast<int>(COMPASS_RANGE / BORDER_CELL_SIZE) + 1;
        const int player_cx = static_cast<int>(floorf(player_pos.x / BORDER_CELL_SIZE));
        const int player_cy = static_cast<int>(floorf(player_pos.y / BORDER_CELL_SIZE));
        const float range_sq = COMPASS_RANGE * COMPASS_RANGE;

        for (int dy = -range_cells; dy <= range_cells; dy++) {
            for (int dx = -range_cells; dx <= range_cells; dx++) {
                const int gx = player_cx + dx;
                const int gy = player_cy + dy;
                const float cell_center_x = (gx + 0.5f) * BORDER_CELL_SIZE;
                const float cell_center_y = (gy + 0.5f) * BORDER_CELL_SIZE;
                const float ddx = cell_center_x - player_pos.x;
                const float ddy = cell_center_y - player_pos.y;
                if (ddx * ddx + ddy * ddy > range_sq) continue;
                const int idx = GetCellIndex(gx, gy);
                if (idx >= 0) explored_cells[idx] = true;
            }
        }
    }



    bool IsGridCellWalkable(int gx, int gy)
    {
        const int idx = GetCellIndex(gx, gy);
        return idx >= 0 && cached_walkable_grid[idx];
    }

    bool IsCellWalkableInTrapezoid(int gx, int gy, const GW::PathingTrapezoid& trap)
    {
        const float cx0 = gx * BORDER_CELL_SIZE;
        const float cy0 = gy * BORDER_CELL_SIZE;
        const float cx1 = cx0 + BORDER_CELL_SIZE;
        const float cy1 = cy0 + BORDER_CELL_SIZE;

        if (cy1 <= trap.YB || cy0 >= trap.YT) return false;
        const float y_lo = std::max(cy0, trap.YB);
        const float y_hi = std::min(cy1, trap.YT);
        const float height = trap.YT - trap.YB;
        if (height < 0.001f) return cx1 > std::min(trap.XBL, trap.XTL) && cx0 < std::max(trap.XBR, trap.XTR);

        const float t_lo = (y_lo - trap.YB) / height;
        const float t_hi = (y_hi - trap.YB) / height;
        const float left = std::min(trap.XBL + t_lo * (trap.XTL - trap.XBL), trap.XBL + t_hi * (trap.XTL - trap.XBL));
        const float right = std::max(trap.XBR + t_lo * (trap.XTR - trap.XBR), trap.XBR + t_hi * (trap.XTR - trap.XBR));
        return cx1 > left && cx0 < right;
    }

    void BuildStaticMapGeometry()
    {
        static_map_geo.vert_count = 0;
        static_map_geo.inaccessible_count = 0;
        static_map_geo.border_count = 0;

        // Border thickness in game units, derived from current zoom so it appears
        // as a consistent pixel width regardless of zoom level.
        // px_to_game at the current zoom = 1 / (mission_map_scale.x * mission_map_zoom * world_map_units_per_game_unit)
        // We can recover this from g2s directly since it's already been rebuilt this frame.
        constexpr float TARGET_THICKNESS_PX = 1.f;
        cached_px_to_game = g2s.valid ? 1.0f / sqrtf(g2s.ax * g2s.ax + g2s.ay * g2s.ay) : BORDER_CELL_SIZE / 600.0f; // fallback if basis not yet built
        const float border_thickness_game = TARGET_THICKNESS_PX * cached_px_to_game;

        const DWORD INACCESSIBLE_COLOR = (DWORD)vq_color_inaccessible;
        const DWORD BORDER_COLOR = (DWORD)vq_color_border;

        static_map_geo.inaccessible_start = 0;

        // 4 strips covering everything outside the grid (scissor rect clips to viewport)
        {
            const float gx0 = cached_grid_x0 * BORDER_CELL_SIZE;
            const float gy0 = cached_grid_y0 * BORDER_CELL_SIZE;
            const float gx1 = (cached_grid_x0 + cached_grid_w) * BORDER_CELL_SIZE;
            const float gy1 = (cached_grid_y0 + cached_grid_h) * BORDER_CELL_SIZE;
            const float ext = std::max(gx1 - gx0, gy1 - gy0) * 5.0f;

            auto push = [&](float x0, float y0, float x1, float y1) {
                StaticMapGeometry::GameVertex* v = static_map_geo.Alloc(6);
                if (!v) return;
                v[0] = {x0, y0, 0.f, INACCESSIBLE_COLOR};
                v[1] = {x1, y0, 0.f, INACCESSIBLE_COLOR};
                v[2] = {x1, y1, 0.f, INACCESSIBLE_COLOR};
                v[3] = {x0, y0, 0.f, INACCESSIBLE_COLOR};
                v[4] = {x1, y1, 0.f, INACCESSIBLE_COLOR};
                v[5] = {x0, y1, 0.f, INACCESSIBLE_COLOR};
                static_map_geo.inaccessible_count += 6;
            };
            push(gx0 - ext, gy0 - ext, gx1 + ext, gy0); // bottom
            push(gx0 - ext, gy1, gx1 + ext, gy1 + ext);  // top
            push(gx0 - ext, gy0, gx0, gy1);               // left
            push(gx1, gy0, gx1 + ext, gy1);               // right
        }

        for (int gy = cached_grid_y0; gy < cached_grid_y0 + cached_grid_h; gy++) {
            for (int gx = cached_grid_x0; gx < cached_grid_x0 + cached_grid_w; gx++) {
                if (IsGridCellWalkable(gx, gy)) continue;
                const float x0 = gx * BORDER_CELL_SIZE;
                const float y0 = gy * BORDER_CELL_SIZE;
                const float x1 = x0 + BORDER_CELL_SIZE;
                const float y1 = y0 + BORDER_CELL_SIZE;
                StaticMapGeometry::GameVertex* v = static_map_geo.Alloc(6);
                if (!v) goto border; // arena full
                v[0] = {x0, y0, 0.f, INACCESSIBLE_COLOR};
                v[1] = {x1, y0, 0.f, INACCESSIBLE_COLOR};
                v[2] = {x1, y1, 0.f, INACCESSIBLE_COLOR};
                v[3] = {x0, y0, 0.f, INACCESSIBLE_COLOR};
                v[4] = {x1, y1, 0.f, INACCESSIBLE_COLOR};
                v[5] = {x0, y1, 0.f, INACCESSIBLE_COLOR};
                static_map_geo.inaccessible_count += 6;
            }
        }

    border:
        static_map_geo.border_start = static_map_geo.vert_count;

        for (const auto& seg : cached_border_segments) {
            const float dx = seg.p2.x - seg.p1.x, dy = seg.p2.y - seg.p1.y;
            const float len_sq = dx * dx + dy * dy;
            if (len_sq < 0.000001f) continue;
            const float inv_len = 1.0f / sqrtf(len_sq);
            const float nx = -dy * inv_len, ny = dx * inv_len;
            const float ix = nx * border_thickness_game, iy = ny * border_thickness_game;
            StaticMapGeometry::GameVertex* v = static_map_geo.Alloc(6);
            if (!v) break;
            v[0] = {seg.p1.x + ix, seg.p1.y + iy, 0.f, BORDER_COLOR};
            v[1] = {seg.p1.x - ix, seg.p1.y - iy, 0.f, BORDER_COLOR};
            v[2] = {seg.p2.x - ix, seg.p2.y - iy, 0.f, BORDER_COLOR};
            v[3] = {seg.p1.x + ix, seg.p1.y + iy, 0.f, BORDER_COLOR};
            v[4] = {seg.p2.x - ix, seg.p2.y - iy, 0.f, BORDER_COLOR};
            v[5] = {seg.p2.x + ix, seg.p2.y + iy, 0.f, BORDER_COLOR};
            static_map_geo.border_count += 6;
        }
    }

    void RebuildMapBorder()
    {
        cached_border_segments.clear();
        delete[] cached_walkable_grid;
        cached_walkable_grid = nullptr;
        cached_walkable_grid_size = 0;
        delete[] explored_cells;
        explored_cells = nullptr;
        explored_map_id = static_cast<GW::Constants::MapID>(0);

        const auto pathing_map = GW::Map::GetPathingMap();
        if (!pathing_map) return;

        std::vector<const GW::PathingTrapezoid*> traps;
        float min_x = FLT_MAX, min_y = FLT_MAX, max_x = -FLT_MAX, max_y = -FLT_MAX;
        for (size_t p = 0; p < pathing_map->size(); p++) {
            const auto& plane = pathing_map->at(p);
            for (uint32_t t = 0; t < plane.trapezoid_count; t++) {
                const auto& trap = plane.trapezoids[t];
                traps.push_back(&trap);
                min_x = std::min({min_x, trap.XTL, trap.XTR, trap.XBL, trap.XBR});
                max_x = std::max({max_x, trap.XTL, trap.XTR, trap.XBL, trap.XBR});
                min_y = std::min(min_y, trap.YB);
                max_y = std::max(max_y, trap.YT);
            }
        }
        if (traps.empty()) return;

        cached_grid_x0 = static_cast<int>(floorf(min_x / BORDER_CELL_SIZE)) - 1;
        cached_grid_y0 = static_cast<int>(floorf(min_y / BORDER_CELL_SIZE)) - 1;
        cached_grid_w = static_cast<int>(ceilf(max_x / BORDER_CELL_SIZE)) + 1 - cached_grid_x0;
        cached_grid_h = static_cast<int>(ceilf(max_y / BORDER_CELL_SIZE)) + 1 - cached_grid_y0;

        cached_walkable_grid_size = cached_grid_w * cached_grid_h;
        cached_walkable_grid = new bool[cached_walkable_grid_size]();

        for (const auto* trap : traps) {
            const int ty0 = std::max(0, static_cast<int>(floorf(trap->YB / BORDER_CELL_SIZE)) - cached_grid_y0);
            const int ty1 = std::min(cached_grid_h - 1, static_cast<int>(ceilf(trap->YT / BORDER_CELL_SIZE)) - cached_grid_y0);
            const int tx0 = std::max(0, static_cast<int>(floorf(std::min({trap->XTL, trap->XBL}) / BORDER_CELL_SIZE)) - cached_grid_x0);
            const int tx1 = std::min(cached_grid_w - 1, static_cast<int>(ceilf(std::max({trap->XTR, trap->XBR}) / BORDER_CELL_SIZE)) - cached_grid_x0);
            for (int cy = ty0; cy <= ty1; cy++) {
                for (int cx = tx0; cx <= tx1; cx++) {
                    const int abs_gx = cached_grid_x0 + cx;
                    const int abs_gy = cached_grid_y0 + cy;
                    const int idx = GetCellIndex(abs_gx, abs_gy);
                    if (idx < 0 || cached_walkable_grid[idx]) continue;
                    if (IsCellWalkableInTrapezoid(abs_gx, abs_gy, *trap)) 
                        cached_walkable_grid[idx] = true;
                }
            }
        }

        for (int cy = 0; cy < cached_grid_h; cy++) {
            for (int cx = 0; cx < cached_grid_w; cx++) {
                const int idx = GetCellIndex(cached_grid_x0 + cx, cached_grid_y0 + cy);
                if (idx < 0 || !cached_walkable_grid[idx]) continue;

                const float x0 = (cached_grid_x0 + cx) * BORDER_CELL_SIZE;
                const float y0 = (cached_grid_y0 + cy) * BORDER_CELL_SIZE;
                const float x1 = x0 + BORDER_CELL_SIZE;
                const float y1 = y0 + BORDER_CELL_SIZE;

                if (!IsGridCellWalkable(cached_grid_x0 + cx, cached_grid_y0 + cy - 1)) cached_border_segments.push_back({{x0, y0, 0}, {x1, y0, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx, cached_grid_y0 + cy + 1)) cached_border_segments.push_back({{x0, y1, 0}, {x1, y1, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx - 1, cached_grid_y0 + cy)) cached_border_segments.push_back({{x0, y0, 0}, {x0, y1, 0}});
                if (!IsGridCellWalkable(cached_grid_x0 + cx + 1, cached_grid_y0 + cy)) cached_border_segments.push_back({{x1, y0, 0}, {x1, y1, 0}});
            }
        }
        BuildStaticMapGeometry();
    }

    void ClearQuestMarker()
    {
        GW::GameThread::Enqueue([] {
            QuestModule::SetCustomQuestMarker({0, 0});
        });
    }

    bool nav_active = false;
    GW::GamePos nav_target_pos;
    GW::GamePos nav_marker_pos;
    bool nav_marker_set = false;
    bool nav_marker_hidden = false;

    constexpr float MARKER_UPDATE_DIST = 500.0f;

    void StopNavigating()
    {
        nav_active = false;
        nav_target_pos = {};
        nav_marker_pos = {};
        nav_marker_set = false;
        nav_marker_hidden = false;
        ClearQuestMarker();
    }

    void SetNavTarget(const GW::GamePos& target)
    {
        nav_active = true;
        nav_target_pos = target;

        const auto player = GW::Agents::GetControlledCharacter();
        if (player) {
            const float dx = target.x - player->pos.x;
            const float dy = target.y - player->pos.y;
            constexpr float HIDE_RANGE = COMPASS_RANGE * 0.5f;
            const bool in_compass = dx * dx + dy * dy < HIDE_RANGE * HIDE_RANGE;

            if (in_compass && !nav_marker_hidden) {
                nav_marker_hidden = true;
                ClearQuestMarker();
                return;
            }
            if (in_compass) return;

            if (nav_marker_hidden) {
                nav_marker_hidden = false;
                nav_marker_set = false;
            }
        }

        const float dx = target.x - nav_marker_pos.x;
        const float dy = target.y - nav_marker_pos.y;
        if (nav_marker_set && dx * dx + dy * dy < MARKER_UPDATE_DIST * MARKER_UPDATE_DIST) return;

        nav_marker_pos = target;
        nav_marker_set = true;
        GW::Vec2f world_pos;
        if (WorldMapWidget::GamePosToWorldMap(target, world_pos)) {
            GW::GameThread::Enqueue([world_pos] {
                QuestModule::SetCustomQuestMarker(world_pos, true);
            });
        }
    }

    void NavigateToClosestEnemy()
    {
        const auto player = GW::Agents::GetControlledCharacter();
        if (!player) return;

        const GW::GamePos player_pos = player->pos;
        float best_dist_sq = FLT_MAX;
        GW::GamePos best_pos = {};
        bool found = false;

        for (const auto& [agent_id, enemy] : tracked_enemies) {
            const float dx = enemy.pos.x - player_pos.x;
            const float dy = enemy.pos.y - player_pos.y;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_pos = enemy.pos;
                found = true;
            }
        }

        if (!found) {
            if (!nav_marker_hidden) {
                nav_marker_hidden = true;
                ClearQuestMarker();
            }
            return;
        }

        SetNavTarget(best_pos);
    }

    void UpdateEnemyTracking()
    {
        const auto map_id = GW::Map::GetMapID();
        const auto instance_type = GW::Map::GetInstanceType();
        if (map_id != tracked_enemies_map_id || instance_type != tracked_enemies_instance_type) {
            tracked_enemies.clear();
            tracked_enemies_map_id = map_id;
            tracked_enemies_instance_type = instance_type;
            if (nav_active) StopNavigating();
        }

        if (instance_type != GW::Constants::InstanceType::Explorable) return;

        const auto player = GW::Agents::GetControlledCharacter();
        if (!player) return;

        const auto agents = GW::Agents::GetAgentArray();
        if (!agents) return;

        std::unordered_set<uint32_t> visible_enemy_ids;

        for (const auto agent : *agents) {
            if (!agent) continue;
            const auto living = agent->GetAsAgentLiving();
            if (!living) continue;
            if (living->allegiance != GW::Constants::Allegiance::Enemy) {
                // Allegiance changed — stop tracking
                tracked_enemies.erase(agent->agent_id);
                continue;
            }
            const auto npc = GW::Agents::GetNPCByID(living->player_number);
            if (npc && (npc->IsSpirit() || npc->IsMinion())) continue;

            visible_enemy_ids.insert(agent->agent_id);

            if (!living->GetIsAlive()) {
                tracked_enemies.erase(agent->agent_id);
                continue;
            }

            auto& tracked = tracked_enemies[agent->agent_id];
            tracked.pos = living->pos;
            tracked.velocity = living->velocity;
            tracked.state = EnemyState::Alive;
        }

        const auto* player_living = player->GetAsAgentLiving();
        static bool was_dead = false;
        const bool is_dead = !player_living || !player_living->GetIsAlive();
        if (is_dead) {
            was_dead = true;
            return;
        }
        if (was_dead) {
            was_dead = false;
            return;
        }

        const auto& player_pos = player->pos;
        for (auto& [agent_id, enemy] : tracked_enemies) {
            if (visible_enemy_ids.contains(agent_id)) continue;
            if (enemy.state != EnemyState::Alive) continue;

            const float dx = enemy.pos.x - player_pos.x;
            const float dy = enemy.pos.y - player_pos.y;
            if (dx * dx + dy * dy < STALE_CHECK_RANGE * STALE_CHECK_RANGE) {
                enemy.state = EnemyState::Stale;
            }
        }

        if (nav_active) NavigateToClosestEnemy();
    }

    GW::Vec2f mission_map_top_left;
    GW::Vec2f mission_map_bottom_right;
    GW::Vec2f mission_map_scale;
    float mission_map_zoom;

    GW::Vec2f mission_map_center_pos;
    GW::Vec2f mission_map_last_click_location;
    GW::Vec2f current_pan_offset;

    GW::Vec2f mission_map_screen_pos;
    GW::Vec2f world_map_click_pos;

    MinimapRenderContext mission_map_render_context;

    bool right_clicking = false;

    GW::UI::Frame* mission_map_frame = nullptr;

        struct FogGeometry {
        struct GameVertex {
            float x, y, z;
            DWORD color;
        };

        static constexpr int MAX_VERTS = 1 << 19; // 512k — fog is smaller than static geo
        GameVertex verts[MAX_VERTS];
        int vert_count = 0;

        GameVertex* Alloc(int count)
        {
            if (vert_count + count > MAX_VERTS) return nullptr;
            GameVertex* ptr = verts + vert_count;
            vert_count += count;
            return ptr;
        }
    } fog_geo;


    bool IsScreenPosOnMissionMap(const GW::Vec2f& screen_pos)
    {
        if (!(mission_map_frame && mission_map_frame->IsVisible())) return false;
        return (screen_pos.x >= mission_map_top_left.x && screen_pos.x <= mission_map_bottom_right.x && screen_pos.y >= mission_map_top_left.y && screen_pos.y <= mission_map_bottom_right.y);
    }

    GW::Vec2f GetMissionMapScreenCenterPos()
    {
        return mission_map_top_left + (mission_map_bottom_right - mission_map_top_left) / 2;
    }

    bool WorldMapCoordsToMissionMapScreenPos(const GW::Vec2f& world_map_position, GW::Vec2f& screen_coords)
    {
        const auto offset = (world_map_position - current_pan_offset);
        const auto scaled_offset = GW::Vec2f(offset.x * mission_map_scale.x, offset.y * mission_map_scale.y);
        screen_coords = (scaled_offset * mission_map_zoom + mission_map_screen_pos);
        return true;
    }

    GW::Vec2f ScreenPosToMissionMapCoords(const GW::Vec2f screen_position)
    {
        GW::Vec2f unscaled_offset = (screen_position - mission_map_screen_pos) / mission_map_zoom;
        GW::Vec2f offset(unscaled_offset.x / mission_map_scale.x, unscaled_offset.y / mission_map_scale.y);
        return offset + current_pan_offset;
    }

    // Used for screen-space geometry (lines, enemy markers, viewport culling)
    bool GamePosToMissionMapScreenPos(const GW::GamePos& game_map_position, GW::Vec2f& screen_coords)
    {
        GW::Vec2f world_map_pos;
        return WorldMapWidget::GamePosToWorldMap(game_map_position, world_map_pos) && WorldMapCoordsToMissionMapScreenPos(world_map_pos, screen_coords);
    }

    void Draw(IDirect3DDevice9*);

    std::vector<GW::UI::UIMessage> messages_hit;
    GW::UI::UIInteractionCallback OnMissionMap_UICallback_Func = nullptr, OnMissionMap_UICallback_Ret = nullptr;

    void OnMissionMap_UICallback(GW::UI::InteractionMessage* message, void* wparam, void* lparam)
    {
        GW::Hook::EnterHook();
        if (message->message_id == GW::UI::UIMessage::kDestroyFrame) mission_map_frame = nullptr;
        OnMissionMap_UICallback_Ret(message, wparam, lparam);
        if (message->message_id == GW::UI::UIMessage::kInitFrame) mission_map_frame = GW::UI::GetFrameById(message->frame_id);
        GW::Hook::LeaveHook();
    }

    bool HookMissionMapFrame()
    {
        if (OnMissionMap_UICallback_Func) return true;

        const auto mission_map_context = GW::Map::GetMissionMapContext();
        mission_map_frame = mission_map_context ? GW::UI::GetFrameById(mission_map_context->frame_id) : nullptr;
        if (!(mission_map_frame && mission_map_frame->frame_callbacks[0].callback)) return false;
        OnMissionMap_UICallback_Func = mission_map_frame->frame_callbacks[0].callback;
        GW::Hook::CreateHook((void**)&OnMissionMap_UICallback_Func, OnMissionMap_UICallback, (void**)&OnMissionMap_UICallback_Ret);
        GW::Hook::EnableHooks(OnMissionMap_UICallback_Func);
        return true;
    }

    bool InitializeMissionMapParameters()
    {
        const auto gameplay_context = GW::GetGameplayContext();
        const auto mission_map_context = GW::Map::GetMissionMapContext();
        if (!(gameplay_context && mission_map_frame && mission_map_frame->IsVisible())) return false;

        const auto root = GW::UI::GetRootFrame();
        mission_map_top_left = mission_map_frame->position.GetContentTopLeft(root);
        mission_map_bottom_right = mission_map_frame->position.GetContentBottomRight(root);
        mission_map_scale = mission_map_frame->position.GetViewportScale(root);
        mission_map_zoom = gameplay_context->mission_map_zoom;
        mission_map_center_pos = mission_map_context->player_mission_map_pos;
        mission_map_last_click_location = mission_map_context->last_mouse_location;
        current_pan_offset = mission_map_context->h003c->mission_map_pan_offset;
        mission_map_screen_pos = GetMissionMapScreenCenterPos();
        return true;
    }

    bool MissionMapContextMenu(void*)
    {
        if (!(mission_map_frame && mission_map_frame->IsVisible())) return false;
        const auto c = ImGui::GetCurrentContext();
        auto viewport_offset = c->CurrentViewport->Pos;
        viewport_offset.x *= -1;
        viewport_offset.y *= -1;

        ImGui::Text("%.2f, %.2f", world_map_click_pos.x, world_map_click_pos.y);
#ifdef _DEBUG
        GW::GamePos game_pos;
        if (WorldMapWidget::WorldMapToGamePos(world_map_click_pos, game_pos)) {
            ImGui::Text("%.2f, %.2f", game_pos.x, game_pos.y);
        }
#endif
        if (ImGui::Button("Place Marker")) {
            nav_active = false;
            nav_target_pos = {};
            nav_marker_pos = {};
            GW::GameThread::Enqueue([] {
                QuestModule::SetCustomQuestMarker(world_map_click_pos, true);
            });
            return false;
        }
        if (QuestModule::GetCustomQuestMarker() && !nav_active) {
            if (ImGui::Button("Remove Marker")) {
                ClearQuestMarker();
                return false;
            }
        }
        if (show_vq_overlay) {
            if (nav_active) {
                if (ImGui::Button("Stop navigating")) {
                    StopNavigating();
                    return false;
                }
            }
            else {
                if (ImGui::Button("Navigate to closest enemy")) {
                    nav_active = true;
                    NavigateToClosestEnemy();
                    return false;
                }
            }
        }

        return true;
    }

    // ---------------------------------------------------------------------------
    // Matrix helpers
    // ---------------------------------------------------------------------------

    // Screen-space ortho: maps pixel coords [0,w]x[0,h] -> NDC [-1,1]x[1,-1]
    D3DMATRIX MakeOrthoProjection(float w, float h)
    {
        // clang-format off
        D3DMATRIX m = {{
             2.f/w,   0.f,  0.f, 0.f,
              0.f,  -2.f/h, 0.f, 0.f,
              0.f,   0.f,  1.f, 0.f,
             -1.f,   1.f,  0.f, 1.f
        }};
        // clang-format on
        return m;
    }



    // Build a world transform that maps raw game coords (x, y) directly to screen
    // pixels, so VQ geometry can be built in game space with no per-vertex projection.
    //
    // The full chain is:
    //   game -> world_map  (WorldMapWidget::GamePosToWorldMap — linear scale+offset)
    //   world_map -> screen (pan, scale, zoom as in WorldMapCoordsToMissionMapScreenPos)
    //
    // We sample three anchor points to recover the affine coefficients, then pack
    // them into a D3D world matrix. The ortho projection handles the final
    // screen->NDC step as before.
    //
    // Returns false if the world-map transform is unavailable this frame.
    bool BuildGameToScreenWorldMatrix(D3DMATRIX& out)
    {
        // Sample the transform at the origin and one unit along each axis
        const GW::GamePos g00 = {0.f, 0.f, 0};
        const GW::GamePos g10 = {1.f, 0.f, 0};
        const GW::GamePos g01 = {0.f, 1.f, 0};

        GW::Vec2f s00, s10, s01;
        if (!GamePosToMissionMapScreenPos(g00, s00) || !GamePosToMissionMapScreenPos(g10, s10) || !GamePosToMissionMapScreenPos(g01, s01)) return false;

        // Basis vectors: screen delta per 1 game unit along each axis
        const float ax = s10.x - s00.x; // d(screen.x)/d(game.x)
        const float ay = s10.y - s00.y; // d(screen.y)/d(game.x)
        const float bx = s01.x - s00.x; // d(screen.x)/d(game.y)
        const float by = s01.y - s00.y; // d(screen.y)/d(game.y)

        // Row-major D3D world matrix (applied as: screen_pos = game_pos * M):
        //   row0 = (ax, ay, 0, 0)    <- game.x contribution
        //   row1 = (bx, by, 0, 0)    <- game.y contribution
        //   row2 = (0,  0,  1, 0)    <- z pass-through (unused for 2D overlay)
        //   row3 = (tx, ty, 0, 1)    <- translation (screen pos of game origin)
        // clang-format off
        out = {{
            ax,     ay,    0.f, 0.f,
            bx,     by,    0.f, 0.f,
            0.f,    0.f,   1.f, 0.f,
            s00.x,  s00.y, 0.f, 1.f
        }};
        // clang-format on
        return true;
    }
    
    // -----------------------------------------------------------------------
    // Vertex buffer types and arena
    // -----------------------------------------------------------------------
    struct VertexBuffers {
        struct GameVertex {
            float x, y, z;
            DWORD color;
        }; // D3DFVF_XYZ | D3DFVF_DIFFUSE
        struct Vertex {
            float x, y, z, w;
            DWORD color;
        }; // D3DFVF_XYZRHW | D3DFVF_DIFFUSE

        static constexpr int MAX_GAME_VERTICES = 1 << 20;   // ~20MB — fog + border
        static constexpr int MAX_SCREEN_VERTICES = 1 << 18; // ~5MB  — lines + enemies

        GameVertex game_arena[MAX_GAME_VERTICES];
        Vertex screen_arena[MAX_SCREEN_VERTICES];

        int game_arena_pos = 0;
        int screen_arena_pos = 0;

        int fog_start = 0, fog_count = 0;
        int line_start = 0, line_count = 0;
        int enemy_start = 0, enemy_count = 0;

        void Reset()
        {
            game_arena_pos = screen_arena_pos = 0;
            fog_count = line_count = enemy_count = 0;
            fog_start = line_start = enemy_start = 0;
        }

        bool Any() const { return fog_count || line_count || enemy_count; }

        GameVertex* GameAlloc(int count)
        {
            if (game_arena_pos + count > MAX_GAME_VERTICES) return nullptr;
            GameVertex* ptr = game_arena + game_arena_pos;
            game_arena_pos += count;
            return ptr;
        }

        Vertex* ScreenAlloc(int count)
        {
            if (screen_arena_pos + count > MAX_SCREEN_VERTICES) return nullptr;
            Vertex* ptr = screen_arena + screen_arena_pos;
            screen_arena_pos += count;
            return ptr;
        }
    };

    // -----------------------------------------------------------------------
    // DrawEnemyCountLabel — ImGui overlay showing VQ kill counts
    // -----------------------------------------------------------------------
    void DrawEnemyCountLabel()
    {
        if (!show_vq_overlay) return;
        if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable) return;

        int alive_count = 0, stale_count = 0;
        for (const auto& [id, enemy] : tracked_enemies) {
            if (enemy.state == EnemyState::Alive)
                alive_count++;
            else if (enemy.state == EnemyState::Stale)
                stale_count++;
        }

        const uint32_t foes_remaining = GW::Map::GetFoesToKill();
        const bool has_vq_data = foes_remaining > 0 || GW::Map::GetFoesKilled() > 0;

        if (!alive_count && !stale_count && !(has_vq_data && foes_remaining > 0)) return;

        char label[128];
        int pos = 0;
        if (alive_count > 0 || stale_count > 0) {
            pos += snprintf(label + pos, sizeof(label) - pos, "%d", alive_count);
            if (stale_count > 0) pos += snprintf(label + pos, sizeof(label) - pos, " (+%d?)", stale_count);
            if (has_vq_data) pos += snprintf(label + pos, sizeof(label) - pos, " / %u remaining", foes_remaining);
        }
        else {
            pos += snprintf(label + pos, sizeof(label) - pos, "%u remaining", foes_remaining);
        }

        constexpr float PADDING = 8.0f;
        const float btn_size = ImGui::GetTextLineHeight() + 12.0f;
        const ImVec2 text_pos(mission_map_top_left.x + PADDING + btn_size, mission_map_bottom_right.y - ImGui::GetTextLineHeight() - PADDING);

        auto* draw_list = ImGui::GetBackgroundDrawList();
        draw_list->AddText({text_pos.x + 1, text_pos.y + 1}, IM_COL32(0, 0, 0, 220), label);
        draw_list->AddText({text_pos.x - 1, text_pos.y - 1}, IM_COL32(0, 0, 0, 220), label);
        draw_list->AddText({text_pos.x + 1, text_pos.y - 1}, IM_COL32(0, 0, 0, 220), label);
        draw_list->AddText({text_pos.x - 1, text_pos.y + 1}, IM_COL32(0, 0, 0, 220), label);
        draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), label);
    }

    // -----------------------------------------------------------------------
    // SubmitVertexBuffers — D3D state setup, draw calls, and restore
    // -----------------------------------------------------------------------
    void SubmitVertexBuffers(IDirect3DDevice9* dx_device, const VertexBuffers& vb)
    {
        if (!vb.Any()) return;

        DWORD oldAlphaBlend, oldSrcBlend, oldDestBlend, oldScissorTest, oldFVF, oldLighting, oldZEnable;
        D3DMATRIX oldWorld, oldView, oldProj;
        RECT oldScissorRect;

        dx_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
        dx_device->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
        dx_device->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
        dx_device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorTest);
        dx_device->GetRenderState(D3DRS_LIGHTING, &oldLighting);
        dx_device->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
        dx_device->GetScissorRect(&oldScissorRect);
        dx_device->GetFVF(&oldFVF);
        dx_device->GetTransform(D3DTS_WORLD, &oldWorld);
        dx_device->GetTransform(D3DTS_VIEW, &oldView);
        dx_device->GetTransform(D3DTS_PROJECTION, &oldProj);

        dx_device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        dx_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dx_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dx_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dx_device->SetRenderState(D3DRS_LIGHTING, FALSE);
        dx_device->SetRenderState(D3DRS_ZENABLE, FALSE);

        RECT scissorRect;
        scissorRect.left = static_cast<LONG>(mission_map_top_left.x);
        scissorRect.top = static_cast<LONG>(mission_map_top_left.y);
        scissorRect.right = static_cast<LONG>(mission_map_bottom_right.x);
        scissorRect.bottom = static_cast<LONG>(mission_map_bottom_right.y);
        dx_device->SetScissorRect(&scissorRect);

        // Pass 1: static map geometry + dynamic VQ (game coords via world matrix + ortho)
        if (static_map_geo.Any() || fog_geo.vert_count || vb.fog_count) {
            D3DVIEWPORT9 vp;
            dx_device->GetViewport(&vp);
            const D3DMATRIX ortho = MakeOrthoProjection(static_cast<float>(vp.Width), static_cast<float>(vp.Height));

            // clang-format off
        const D3DMATRIX gameToScreen = {{
            g2s.ax, g2s.ay, 0.f, 0.f,
            g2s.bx, g2s.by, 0.f, 0.f,
            0.f,    0.f,    1.f, 0.f,
            g2s.ox, g2s.oy, 0.f, 1.f
        }};
            // clang-format on

            dx_device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
            dx_device->SetTransform(D3DTS_WORLD, &gameToScreen);
            dx_device->SetTransform(D3DTS_VIEW, &IDENTITY_MATRIX);
            dx_device->SetTransform(D3DTS_PROJECTION, &ortho);

            // Static (rebuilt on map change only)
            if (static_map_geo.inaccessible_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_map_geo.inaccessible_count / 3, static_map_geo.verts + static_map_geo.inaccessible_start, sizeof(StaticMapGeometry::GameVertex));
            if (static_map_geo.border_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_map_geo.border_count / 3, static_map_geo.verts + static_map_geo.border_start, sizeof(StaticMapGeometry::GameVertex));

            // Static fog (rebuilt when player moves)
            if (fog_geo.vert_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, fog_geo.vert_count / 3, fog_geo.verts, sizeof(FogGeometry::GameVertex));

            // Dynamic per-frame game-coord geometry (compass circle)
            if (vb.fog_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, vb.fog_count / 3, vb.game_arena + vb.fog_start, sizeof(VertexBuffers::GameVertex));
        }

        // Pass 2: Screen-space geometry — XYZRHW bypasses transform pipeline
        if (vb.line_count || vb.enemy_count) {
            dx_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

            if (vb.line_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, vb.line_count / 3, vb.screen_arena + vb.line_start, sizeof(VertexBuffers::Vertex));
            if (vb.enemy_count) dx_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, vb.enemy_count / 3, vb.screen_arena + vb.enemy_start, sizeof(VertexBuffers::Vertex));
        }

        dx_device->SetFVF(oldFVF);
        dx_device->SetTransform(D3DTS_WORLD, &oldWorld);
        dx_device->SetTransform(D3DTS_VIEW, &oldView);
        dx_device->SetTransform(D3DTS_PROJECTION, &oldProj);
        dx_device->SetRenderState(D3DRS_ZENABLE, oldZEnable);
        dx_device->SetRenderState(D3DRS_LIGHTING, oldLighting);
        dx_device->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
        dx_device->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
        dx_device->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
        dx_device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorTest);
        dx_device->SetScissorRect(&oldScissorRect);
    }
    // -----------------------------------------------------------------------
    // Primitive enqueue functions — each writes directly into vb and returns
    // the number of vertices added (0 if the arena was full).
    // -----------------------------------------------------------------------

    // Screen-space thick line (XYZRHW). antialias=true adds transparent fringe quads.
    int EnqueueThickLine(VertexBuffers& vb, int& out_count, const GW::Vec2f& s1, const GW::Vec2f& s2, float half_thickness, DWORD color, bool antialias = false)
    {
        const float dx = s2.x - s1.x, dy = s2.y - s1.y;
        const float len_sq = dx * dx + dy * dy;
        if (len_sq < 0.000001f) return 0;

        const float inv_len = 1.0f / sqrtf(len_sq);
        const float nx = -dy * inv_len, ny = dx * inv_len;
        const float ix = nx * half_thickness, iy = ny * half_thickness;

        if (!antialias) {
            VertexBuffers::Vertex* v = vb.ScreenAlloc(6);
            if (!v) return 0;
            v[0] = {s1.x + ix, s1.y + iy, 0.f, 1.f, color};
            v[1] = {s1.x - ix, s1.y - iy, 0.f, 1.f, color};
            v[2] = {s2.x - ix, s2.y - iy, 0.f, 1.f, color};
            v[3] = {s1.x + ix, s1.y + iy, 0.f, 1.f, color};
            v[4] = {s2.x - ix, s2.y - iy, 0.f, 1.f, color};
            v[5] = {s2.x + ix, s2.y + iy, 0.f, 1.f, color};
            out_count += 6;
            return 6;
        }

        const float io = half_thickness + 1.0f;
        const float ox = nx * io, oy = ny * io;
        const DWORD t = color & 0x00FFFFFF;

        VertexBuffers::Vertex* v = vb.ScreenAlloc(12);
        if (!v) return 0;
        v[0] = {s1.x + ox, s1.y + oy, 0.f, 1.f, t};
        v[1] = {s1.x - ox, s1.y - oy, 0.f, 1.f, t};
        v[2] = {s2.x - ox, s2.y - oy, 0.f, 1.f, t};
        v[3] = {s1.x + ox, s1.y + oy, 0.f, 1.f, t};
        v[4] = {s2.x - ox, s2.y - oy, 0.f, 1.f, t};
        v[5] = {s2.x + ox, s2.y + oy, 0.f, 1.f, t};
        v[6] = {s1.x + ix, s1.y + iy, 0.f, 1.f, color};
        v[7] = {s1.x - ix, s1.y - iy, 0.f, 1.f, color};
        v[8] = {s2.x - ix, s2.y - iy, 0.f, 1.f, color};
        v[9] = {s1.x + ix, s1.y + iy, 0.f, 1.f, color};
        v[10] = {s2.x - ix, s2.y - iy, 0.f, 1.f, color};
        v[11] = {s2.x + ix, s2.y + iy, 0.f, 1.f, color};
        out_count += 12;
        return 12;
    }

    // Game-coord thick line (XYZ). half_thickness is in game units.
    int EnqueueThickLineGame(VertexBuffers& vb, int& out_count, float x1, float y1, float x2, float y2, float half_thickness, DWORD color)
    {
        const float dx = x2 - x1, dy = y2 - y1;
        const float len_sq = dx * dx + dy * dy;
        if (len_sq < 0.000001f) return 0;

        const float inv_len = 1.0f / sqrtf(len_sq);
        const float nx = -dy * inv_len, ny = dx * inv_len;
        const float ix = nx * half_thickness, iy = ny * half_thickness;

        VertexBuffers::GameVertex* v = vb.GameAlloc(6);
        if (!v) return 0;
        v[0] = {x1 + ix, y1 + iy, 0.f, color};
        v[1] = {x1 - ix, y1 - iy, 0.f, color};
        v[2] = {x2 - ix, y2 - iy, 0.f, color};
        v[3] = {x1 + ix, y1 + iy, 0.f, color};
        v[4] = {x2 - ix, y2 - iy, 0.f, color};
        v[5] = {x2 + ix, y2 + iy, 0.f, color};
        out_count += 6;
        return 6;
    }

    // Game-coord axis-aligned quad (XYZ).
    int EnqueueGameQuad(VertexBuffers& vb, int& out_count, float x0, float y0, float x1, float y1, DWORD color)
    {
        VertexBuffers::GameVertex* v = vb.GameAlloc(6);
        if (!v) return 0;
        v[0] = {x0, y0, 0.f, color};
        v[1] = {x1, y0, 0.f, color};
        v[2] = {x1, y1, 0.f, color};
        v[3] = {x0, y0, 0.f, color};
        v[4] = {x1, y1, 0.f, color};
        v[5] = {x0, y1, 0.f, color};
        out_count += 6;
        return 6;
    }

    // Screen-space diamond (two triangles, 6 verts).
    int EnqueueDiamond(VertexBuffers& vb, int& out_count, float cx, float cy, float size, DWORD color)
    {
        VertexBuffers::Vertex* v = vb.ScreenAlloc(6);
        if (!v) return 0;
        v[0] = {cx, cy - size, 0.f, 1.f, color};
        v[1] = {cx + size, cy, 0.f, 1.f, color};
        v[2] = {cx, cy + size, 0.f, 1.f, color};
        v[3] = {cx, cy + size, 0.f, 1.f, color};
        v[4] = {cx - size, cy, 0.f, 1.f, color};
        v[5] = {cx, cy - size, 0.f, 1.f, color};
        out_count += 6;
        return 6;
    }

    // Screen-space radial halo fading from center_color to transparent at edge.
    int EnqueueHalo(VertexBuffers& vb, int& out_count, float cx, float cy, float radius, DWORD center_color)
    {
        constexpr int HALO_SEGMENTS = 12;
        VertexBuffers::Vertex* v = vb.ScreenAlloc(HALO_SEGMENTS * 3);
        if (!v) return 0;
        const DWORD edge_color = center_color & 0x00FFFFFF;
        for (int i = 0; i < HALO_SEGMENTS; i++) {
            const float a1 = static_cast<float>(i) / HALO_SEGMENTS * TWO_PI;
            const float a2 = static_cast<float>(i + 1) / HALO_SEGMENTS * TWO_PI;
            v[i * 3 + 0] = {cx, cy, 0.f, 1.f, center_color};
            v[i * 3 + 1] = {cx + radius * cosf(a1), cy + radius * sinf(a1), 0.f, 1.f, edge_color};
            v[i * 3 + 2] = {cx + radius * cosf(a2), cy + radius * sinf(a2), 0.f, 1.f, edge_color};
        }
        out_count += HALO_SEGMENTS * 3;
        return HALO_SEGMENTS * 3;
    }

    // Screen-space velocity arrow for a stale enemy marker.
    // Returns 0 if the enemy is stationary or the projection fails.
    int EnqueueVelocityArrow(VertexBuffers& vb, int& out_count, float cx, float cy, const TrackedEnemy& enemy)
    {
        constexpr float MARKER_SIZE = 9.0f;
        constexpr float ARROW_LENGTH = 28.0f;
        constexpr float ARROW_HALF_WIDTH = 6.0f;
        constexpr DWORD COLOR_ARROW = D3DCOLOR_ARGB(240, 255, 220, 50);
        constexpr DWORD COLOR_ARROW_OUTLINE = D3DCOLOR_ARGB(240, 0, 0, 0);

        const float vlen_sq = enemy.velocity.x * enemy.velocity.x + enemy.velocity.y * enemy.velocity.y;
        if (vlen_sq < 1.0f) return 0;

        GW::GamePos offset_pos = enemy.pos;
        const float vlen = sqrtf(vlen_sq);
        offset_pos.x += enemy.velocity.x / vlen * 500.0f;
        offset_pos.y += enemy.velocity.y / vlen * 500.0f;
        GW::Vec2f screen_offset;
        if (!GamePosToMissionMapScreenPos(offset_pos, screen_offset)) return 0;

        float dx = screen_offset.x - cx;
        float dy = screen_offset.y - cy;
        const float slen = sqrtf(dx * dx + dy * dy);
        if (slen < 0.1f) return 0;
        dx /= slen;
        dy /= slen;

        const float tip_x = cx + dx * ARROW_LENGTH;
        const float tip_y = cy + dy * ARROW_LENGTH;
        const float base_x = cx + dx * (MARKER_SIZE * 0.5f);
        const float base_y = cy + dy * (MARKER_SIZE * 0.5f);
        const float nx = -dy, ny = dx;
        constexpr float OL = 2.5f;

        VertexBuffers::Vertex* v = vb.ScreenAlloc(6);
        if (!v) return 0;
        v[0] = {tip_x + dx * OL, tip_y + dy * OL, 0.f, 1.f, COLOR_ARROW_OUTLINE};
        v[1] = {base_x + nx * (ARROW_HALF_WIDTH + OL), base_y + ny * (ARROW_HALF_WIDTH + OL), 0.f, 1.f, COLOR_ARROW_OUTLINE};
        v[2] = {base_x - nx * (ARROW_HALF_WIDTH + OL), base_y - ny * (ARROW_HALF_WIDTH + OL), 0.f, 1.f, COLOR_ARROW_OUTLINE};
        v[3] = {tip_x, tip_y, 0.f, 1.f, COLOR_ARROW};
        v[4] = {base_x + nx * ARROW_HALF_WIDTH, base_y + ny * ARROW_HALF_WIDTH, 0.f, 1.f, COLOR_ARROW};
        v[5] = {base_x - nx * ARROW_HALF_WIDTH, base_y - ny * ARROW_HALF_WIDTH, 0.f, 1.f, COLOR_ARROW};
        out_count += 6;
        return 6;
    }

    // Enqueues halo + outline diamond + fill diamond (+ arrow if stale) for one enemy.
    int EnqueueEnemyMarker(VertexBuffers& vb, int& out_count, const TrackedEnemy& enemy)
    {
        constexpr float MARKER_SIZE = 9.0f;
        constexpr float OUTLINE_SIZE = MARKER_SIZE + 2.0f;
        constexpr float HALO_SIZE = MARKER_SIZE + 8.0f;
        const DWORD COLOR_OUTLINE = (DWORD)vq_color_enemy_outline;
        const DWORD COLOR_ALIVE = (DWORD)vq_color_enemy_alive;
        const DWORD COLOR_STALE = (DWORD)vq_color_enemy_stale;

        GW::Vec2f screen_pos;
        if (!GamePosToMissionMapScreenPos(enemy.pos, screen_pos)) return 0;

        const bool is_stale = enemy.state == EnemyState::Stale;
        const DWORD color = is_stale ? COLOR_STALE : COLOR_ALIVE;
        const DWORD halo_color = is_stale ? D3DCOLOR_ARGB(60, 255, 180, 50) : D3DCOLOR_ARGB(80, 70, 130, 255);
        const float cx = screen_pos.x, cy = screen_pos.y;

        int added = 0;
        added += EnqueueHalo(vb, out_count, cx, cy, HALO_SIZE, halo_color);
        added += EnqueueDiamond(vb, out_count, cx, cy, OUTLINE_SIZE, COLOR_OUTLINE);
        added += EnqueueDiamond(vb, out_count, cx, cy, MARKER_SIZE, color);
        if (is_stale) added += EnqueueVelocityArrow(vb, out_count, cx, cy, enemy);
        return added;
    }

    // -----------------------------------------------------------------------
    // EnqueueEnemyMarkers — writes all enemy markers into vb.screen_arena
    // -----------------------------------------------------------------------
    void EnqueueEnemyMarkers(VertexBuffers& vb)
    {
        if (!show_vq_overlay) return;
        if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable) return;

        // Stale first (drawn below alive)
        for (const auto& [agent_id, enemy] : tracked_enemies) {
            if (enemy.state == EnemyState::Stale) EnqueueEnemyMarker(vb, vb.enemy_count, enemy);
        }
        for (const auto& [agent_id, enemy] : tracked_enemies) {
            if (enemy.state == EnemyState::Alive) EnqueueEnemyMarker(vb, vb.enemy_count, enemy);
        }
    }

    // Returns true if the cell at flat local grid index `idx` is walkable and explored,
    // making the shared edge a frontier border. Caller is responsible for bounds checking.
    bool IsFrontierEdge(int idx)
    {
        if (!explored_cells) return false;
        return explored_cells[idx];
    }


    int last_fog_player_cx = INT_MIN;
    int last_fog_player_cy = INT_MIN;

    void BuildFogGeometry()
    {
        fog_geo.vert_count = 0;

        if (!explored_cells || !cached_walkable_grid) return;
        const DWORD FOG_UNEXPLORED = (DWORD)vq_color_fog_unexplored;
        const DWORD FRONTIER_COLOR = (DWORD)vq_color_frontier;

        constexpr float FRONTIER_HALF_THICKNESS = 1.5f;
        const float ft = FRONTIER_HALF_THICKNESS * cached_px_to_game; // frontier thickness in game units

        const float grid_origin_x = cached_grid_x0 * BORDER_CELL_SIZE;
        const float grid_origin_y = cached_grid_y0 * BORDER_CELL_SIZE;

        // Inline helper — writes a horizontal frontier edge (normal is along Y axis)
        // ny = +ft for south-facing edge, -ft for north-facing edge
        const auto write_h_edge = [&](float x0, float y, float x1, float ny_sign) -> bool {
            FogGeometry::GameVertex* v = fog_geo.Alloc(6);
            if (!v) return false;
            const float oy = ny_sign * ft;
            v[0] = {x0, y - oy, 0.f, FRONTIER_COLOR};
            v[1] = {x0, y + oy, 0.f, FRONTIER_COLOR};
            v[2] = {x1, y + oy, 0.f, FRONTIER_COLOR};
            v[3] = {x0, y - oy, 0.f, FRONTIER_COLOR};
            v[4] = {x1, y + oy, 0.f, FRONTIER_COLOR};
            v[5] = {x1, y - oy, 0.f, FRONTIER_COLOR};
            return true;
        };

        // Inline helper — writes a vertical frontier edge (normal is along X axis)
        const auto write_v_edge = [&](float x, float y0, float y1, float nx_sign) -> bool {
            FogGeometry::GameVertex* v = fog_geo.Alloc(6);
            if (!v) return false;
            const float ox = nx_sign * ft;
            v[0] = {x - ox, y0, 0.f, FRONTIER_COLOR};
            v[1] = {x + ox, y0, 0.f, FRONTIER_COLOR};
            v[2] = {x + ox, y1, 0.f, FRONTIER_COLOR};
            v[3] = {x - ox, y0, 0.f, FRONTIER_COLOR};
            v[4] = {x + ox, y1, 0.f, FRONTIER_COLOR};
            v[5] = {x - ox, y1, 0.f, FRONTIER_COLOR};
            return true;
        };

        for (int gy = 0; gy < cached_grid_h; gy++) {
            const float y0 = grid_origin_y + gy * BORDER_CELL_SIZE;
            const float y1 = y0 + BORDER_CELL_SIZE;
            const int row_base = gy * cached_grid_w;

            for (int gx = 0; gx < cached_grid_w; gx++) {
                const int idx = row_base + gx;
                if (!cached_walkable_grid[idx]) continue;
                if (explored_cells && explored_cells[idx]) continue;

                const float x0 = grid_origin_x + gx * BORDER_CELL_SIZE;
                const float x1 = x0 + BORDER_CELL_SIZE;

                // Unexplored fog quad
                FogGeometry::GameVertex* v = fog_geo.Alloc(6);
                if (!v) return;
                v[0] = {x0, y0, 0.f, FOG_UNEXPLORED};
                v[1] = {x1, y0, 0.f, FOG_UNEXPLORED};
                v[2] = {x1, y1, 0.f, FOG_UNEXPLORED};
                v[3] = {x0, y0, 0.f, FOG_UNEXPLORED};
                v[4] = {x1, y1, 0.f, FOG_UNEXPLORED};
                v[5] = {x0, y1, 0.f, FOG_UNEXPLORED};


                // North edge (y0, normal faces -Y)
                if (gy > 0 && IsFrontierEdge(row_base - cached_grid_w + gx))
                    if (!write_h_edge(x0, y0, x1, -1.f)) return;

                // South edge (y1, normal faces +Y)
                if (gy < cached_grid_h - 1 && IsFrontierEdge(row_base + cached_grid_w + gx))
                    if (!write_h_edge(x0, y1, x1, +1.f)) return;

                // West edge (x0, normal faces -X)
                if (gx > 0 && IsFrontierEdge(row_base + gx - 1))
                    if (!write_v_edge(x0, y0, y1, -1.f)) return;

                // East edge (x1, normal faces +X)
                if (gx < cached_grid_w - 1 && IsFrontierEdge(row_base + gx + 1))
                    if (!write_v_edge(x1, y0, y1, +1.f)) return;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Draw — builds geometry into VertexBuffers, then submits
    // -----------------------------------------------------------------------
    void Draw(IDirect3DDevice9* dx_device)
    {
        if (!HookMissionMapFrame()) return;
        if (!InitializeMissionMapParameters()) return;
        g2s.Rebuild();
        if (!g2s.valid) return;

        static VertexBuffers vb;
        vb.Reset();



        // -----------------------------------------------------------------------
        // Custom renderer lines — screen space
        // -----------------------------------------------------------------------
        const auto& lines = Minimap::Instance().custom_renderer.GetLines();
        const auto map_id = GW::Map::GetMapID();

        vb.line_start = vb.screen_arena_pos;
        constexpr float LINE_HALF_THICKNESS = 1.5f;

        for (const auto& line : lines) {
            if (!line->visible) continue;
            if (!line->draw_on_mission_map && !(draw_all_minimap_lines && line->draw_on_minimap) && !(draw_all_terrain_lines && line->draw_on_terrain)) continue;
            if (line->map != map_id) continue;

            GW::Vec2f projected_p1, projected_p2;
            if (!GamePosToMissionMapScreenPos(line->p1, projected_p1)) continue;
            if (!GamePosToMissionMapScreenPos(line->p2, projected_p2)) continue;

            EnqueueThickLine(vb, vb.line_count, projected_p1, projected_p2, LINE_HALF_THICKNESS, static_cast<DWORD>(line->color));
        }

        // -----------------------------------------------------------------------
        // VQ overlay — raw game coords, no per-vertex projection
        // -----------------------------------------------------------------------
        const bool in_explorable = GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable;

        if (show_vq_overlay && in_explorable) {
            const bool map_changed = map_id != border_map_id;
            const bool zoom_changed = mission_map_zoom != border_cached_zoom;

            if (map_changed || zoom_changed) {
                if (map_changed) {
                    border_map_id = map_id;
                    last_fog_player_cx = INT_MIN;
                    last_fog_player_cy = INT_MIN;
                    RebuildMapBorder(); // rebuilds walkable grid + border segments
                }
                border_cached_zoom = mission_map_zoom;
                BuildStaticMapGeometry(); // rebuilds static vertex cache with correct thickness
                BuildFogGeometry();
            }

            // Compass range circle (per-frame, goes into vb game arena)
            vb.fog_start = vb.game_arena_pos;
            {
                const auto player = GW::Agents::GetControlledCharacter();
                if (player) {
                    constexpr int CIRCLE_SEGMENTS = 64;

                    const DWORD CIRCLE_COLOR = (DWORD)vq_color_compass;

                    constexpr float CIRCLE_THICKNESS = 0.5f;
                    const float px = player->pos.x, py = player->pos.y;
                    const float game_thickness = CIRCLE_THICKNESS * cached_px_to_game;

                    // Precompute all points — each trig result used twice (end of seg N, start of seg N+1)
                    GW::Vec2f pts[CIRCLE_SEGMENTS + 1];
                    for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
                        const float a = static_cast<float>(i) / CIRCLE_SEGMENTS * TWO_PI;
                        pts[i] = {px + COMPASS_RANGE * cosf(a), py + COMPASS_RANGE * sinf(a)};
                    }
                    // pts[0] == pts[CIRCLE_SEGMENTS] so the circle closes cleanly

                    for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
                        EnqueueThickLineGame(vb, vb.fog_count, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, game_thickness, CIRCLE_COLOR);
                    }
                }
            }

            // Fog overlay + frontier — rebuilt only when player cell changes
            {
                const auto player = GW::Agents::GetControlledCharacter();
                if (player) {
                    const int player_cx = static_cast<int>(floorf(player->pos.x / BORDER_CELL_SIZE));
                    const int player_cy = static_cast<int>(floorf(player->pos.y / BORDER_CELL_SIZE));

                    if (player_cx != last_fog_player_cx || player_cy != last_fog_player_cy) {
                        // Don't cache position until explored_cells is ready,
                        // so we keep rebuilding until exploration data arrives.
                        if (explored_cells) {
                            last_fog_player_cx = player_cx;
                            last_fog_player_cy = player_cy;
                        }
                        BuildFogGeometry();
                    }
                }
            }
        }

        // -----------------------------------------------------------------------
        // Enemy markers
        // -----------------------------------------------------------------------
        vb.enemy_start = vb.screen_arena_pos;
        EnqueueEnemyMarkers(vb);

        // -----------------------------------------------------------------------
        // VQ toggle button (ImGui)
        // -----------------------------------------------------------------------
        if (in_explorable) {
            constexpr float PADDING = 4.0f;
            const float btn_size = ImGui::GetTextLineHeight() + PADDING * 2;
            const ImVec2 btn_pos(mission_map_top_left.x + PADDING, mission_map_bottom_right.y - btn_size - PADDING);

            ImGui::SetNextWindowPos(btn_pos);
            ImGui::SetNextWindowSize({0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
            if (ImGui::Begin("##vq_toggle", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                if (show_vq_overlay) {
                    if (ImGui::Button(ICON_FA_SKULL "##vq_off")) show_vq_overlay = false;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("VQ overlay active. Click to hide.");
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    if (ImGui::Button(ICON_FA_SKULL "##vq_on")) show_vq_overlay = true;
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("VQ overlay hidden. Click to show.");
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        SubmitVertexBuffers(dx_device, vb);
        DrawEnemyCountLabel();
    }
} // namespace

void MissionMapWidget::LoadSettings(ToolboxIni* ini)
{
    ToolboxWidget::LoadSettings(ini);
    LOAD_BOOL(draw_all_terrain_lines);
    LOAD_BOOL(draw_all_minimap_lines);
    LOAD_BOOL(show_vq_overlay);

    LOAD_COLOR(vq_color_inaccessible);
    LOAD_COLOR(vq_color_fog_unexplored);
    LOAD_COLOR(vq_color_border);
    LOAD_COLOR(vq_color_frontier);
    LOAD_COLOR(vq_color_compass);
    LOAD_COLOR(vq_color_enemy_alive);
    LOAD_COLOR(vq_color_enemy_stale);
    LOAD_COLOR(vq_color_enemy_outline);
}

void MissionMapWidget::SaveSettings(ToolboxIni* ini)
{
    ToolboxWidget::SaveSettings(ini);
    SAVE_BOOL(draw_all_terrain_lines);
    SAVE_BOOL(draw_all_minimap_lines);
    SAVE_BOOL(show_vq_overlay);

    SAVE_COLOR(vq_color_inaccessible);
    SAVE_COLOR(vq_color_fog_unexplored);
    SAVE_COLOR(vq_color_border);
    SAVE_COLOR(vq_color_frontier);
    SAVE_COLOR(vq_color_compass);
    SAVE_COLOR(vq_color_enemy_alive);
    SAVE_COLOR(vq_color_enemy_stale);
    SAVE_COLOR(vq_color_enemy_outline);
}

void MissionMapWidget::Draw(IDirect3DDevice9* dx_device)
{
    if (show_vq_overlay) {
        // Throttle tracking updates — no need to run every render frame.
        // Enemy positions update at ~10Hz in GW anyway.
        static DWORD last_tracking_tick = 0;
        const DWORD now = GetTickCount();
        if (now - last_tracking_tick >= 100) { // 10Hz
            last_tracking_tick = now;
            UpdateEnemyTracking();
            if (const auto player = GW::Agents::GetControlledCharacter()) {
                UpdateExploration(player->pos);
            }
        }
    }
    if (visible) ::Draw(dx_device);
    HookMissionMapFrame();
}

void MissionMapWidget::DrawSettingsInternal()
{
    ImGui::Checkbox("Draw all terrain lines", &draw_all_terrain_lines);
    ImGui::Checkbox("Draw all minimap lines", &draw_all_minimap_lines);
    ImGui::Checkbox("Vanquish Overlay", &show_vq_overlay);
    ImGui::ShowHelp("Tracks enemy positions as they enter compass range.\nBlue = alive, Orange = last known (moved away).\nArrows on orange markers show last movement direction.\nAlso highlights areas you've explored during this session on the mission map.");

    if (show_vq_overlay) {
        ImGui::Indent();
        // Flag that triggers a static geometry rebuild if colour changes
        bool static_changed = false;
        bool fog_changed = false;

        static_changed |= Colors::DrawSettingHueWheel("Inaccessible area", &vq_color_inaccessible);
        static_changed |= Colors::DrawSettingHueWheel("Map border", &vq_color_border);
        fog_changed |= Colors::DrawSettingHueWheel("Unexplored fog", &vq_color_fog_unexplored);
        fog_changed |= Colors::DrawSettingHueWheel("Frontier edge", &vq_color_frontier);
        Colors::DrawSettingHueWheel("Compass range", &vq_color_compass);
        Colors::DrawSettingHueWheel("Enemy (alive)", &vq_color_enemy_alive);
        Colors::DrawSettingHueWheel("Enemy (last known position)", &vq_color_enemy_stale);
        Colors::DrawSettingHueWheel("Enemy outline", &vq_color_enemy_outline);
        ImGui::Unindent();

        if (static_changed) BuildStaticMapGeometry();
        if (fog_changed) BuildFogGeometry();
    }
}

bool MissionMapWidget::WndProc(const UINT Message, WPARAM, LPARAM lParam)
{
    switch (Message) {
        case WM_GW_RBUTTONCLICK:
            GW::Vec2f cursor_pos = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!IsScreenPosOnMissionMap(cursor_pos)) break;
            if (GW::UI::GetCurrentTooltip()) break;
            world_map_click_pos = ScreenPosToMissionMapCoords(cursor_pos);
            ImGui::SetContextMenu(MissionMapContextMenu);
            break;
    }
    return false;
}

void MissionMapWidget::Terminate()
{
    ToolboxWidget::Terminate();
    if (OnMissionMap_UICallback_Func) GW::Hook::RemoveHook(OnMissionMap_UICallback_Func);
    OnMissionMap_UICallback_Func = nullptr;
    delete[] cached_walkable_grid;
    cached_walkable_grid = nullptr;
    delete[] explored_cells;
    explored_cells = nullptr;
}
