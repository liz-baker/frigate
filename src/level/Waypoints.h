#pragma once

#include <cstdint>
#include <vector>

#include "level/BgHeader.h"
#include "level/Stan.h"

namespace Frigate::Level {

class StageSetup;
struct Pad;

// waypoint: a pad the guards' routes pass through. On-disk layout:
// docs/formats/goldeneye-stage-setup.md.
struct Waypoint {
    // The pad it is anchored to.
    int32_t padId = -1;
    // The waypoints it connects to (the file's -1-terminated array, without the -1).
    std::vector<int32_t> neighbours;
    // The waygroup holding it; negative until init_path_table_links numbers it.
    int32_t groupNum = -1;
    // The route search's scratch distance.
    int32_t dist = 0;
};

// waygroup: a set of waypoints, and the groups next to it.
struct Waygroup {
    std::vector<int32_t> neighbours;
    std::vector<int32_t> waypoints;
    int32_t dist = 0;
};

// PathRecord: a patrol (a loop) or a one-way route, as waypoint numbers.
struct PatrolPath {
    std::vector<int32_t> waypoints;
    uint8_t id = 0;
    bool isLoop = false;
    // len: the number of waypoints, which proplvreset2 sets.
    uint16_t len = 0;
};

// The waypoint graph of the stage's setup and the queries the guards' routes
// use (padhalllv.c, init_path_table_links and chrlvStanPathRelated). The
// graph's scratch distances live here; the pads it refers to stay in the
// setup.
class Waypoints {
  public:
    // The most waypoints a route holds (PATH_FINDING_WP_LIMIT).
    static constexpr int kRouteLimit = 6;

    // The setup's waypoints, groups and patrol paths, and the pads they name.
    // `setup` must outlive this.
    void Reset(const StageSetup* setup);

    // init_path_table_links: checks the graph's links, numbers each waypoint's
    // group and leaves the flood-fill distances behind. The game locks up on
    // an error; this returns the number of them, after logging each.
    int InitLinks();

    bool HasWaypoints() const {
        return hasWaypoints_;
    }
    bool HasGroups() const {
        return hasGroups_;
    }
    const std::vector<Waypoint>& All() const {
        return waypoints_;
    }
    const std::vector<Waygroup>& Groups() const {
        return groups_;
    }
    const Waypoint& operator[](int waypoint) const {
        return waypoints_[static_cast<size_t>(waypoint)];
    }
    const std::vector<PatrolPath>& Paths() const {
        return paths_;
    }
    // pathFindById: the index of the patrol path numbered `id`, or -1.
    int FindPath(int id) const;

    // The pad a waypoint sits on, and its position: g_CurrentSetup.pads[padID].
    const Pad& PadOf(int waypoint) const;

    // get_ptrpreset_in_table_matching_tile: the first waypoint whose pad
    // stands on `tile`, or -1.
    int FindOnTile(int tile) const;
    // chrlvPadPresetRelated: the squared distance in the floor plane from a
    // position to a waypoint's pad.
    float PlaneDistanceSquared(const Vec3& position, int waypoint) const;
    // chrlvStanPathRelated: the waypoint nearest `position`, searched outward
    // from `tile` across tile links to the first tile with a waypoint on it;
    // -1 when there is none.
    int NearestTo(Stan& stan, const Vec3& position, int tile) const;

    // waypointFindRoute: the route of waypoints from `from` to `to`, across
    // groups, written to `route` (which holds `capacity` entries) and ended
    // by -1. Returns the number of entries written including the -1.
    int FindRoute(int from, int to, int* route, int capacity);
    // waypointFindRouteInGroup: the route inside one group.
    int FindRouteInGroup(int from, int to, int* route, int capacity);
    // waypointFindNextStepToward: the neighbour of `from` to step to on the
    // way to `to`; -1 when there is none.
    int FindNextStepToward(int from, int to);
    // sub_GAME_7F08F438: a waypoint of group `a` with a neighbour in group
    // `b`, and that neighbour; both -1 when none.
    void FindLink(int groupA, int groupB, int* pointA, int* pointB) const;
    // waypointFindNearestToPos and waypointRefineNearestToPos.
    int FindNearestToPosition(const Vec3& position) const;
    int RefineNearest(const Vec3& position, int waypoint) const;

  private:
    // The waygroup-level search.
    int FindGroupByDist(const std::vector<int32_t>& groups, int dist) const;
    void SetGroupsUnvisited(const std::vector<int32_t>& groups, int value);
    bool PropagateGroupDist(int arg);
    bool FloodGroupDist(int from, int to, bool everything);
    bool MarkGroupRoute(int from, int to);
    int FindGroupRandomByDist(const std::vector<int32_t>& groups, int dist);
    // The waypoint-level search.
    int FindPointByDistInGroup(const std::vector<int32_t>& points, int dist, int group) const;
    void SetPointsUnvisited(const std::vector<int32_t>& points, int value, int group);
    bool PropagatePointDist(const std::vector<int32_t>& points, int arg, int group);
    void Bfs(int from, int to, bool everything);
    void MarkRoute(int from, int to);
    int FindPointRandomByDist(const std::vector<int32_t>& points, int dist);
    void ResetDistances();
    bool ValidGroup(int group) const {
        return group >= 0 && static_cast<size_t>(group) < groups_.size();
    }
    bool ValidPoint(int point) const {
        return point >= 0 && static_cast<size_t>(point) < waypoints_.size();
    }

    const StageSetup* setup_ = nullptr;
    std::vector<Waypoint> waypoints_;
    std::vector<Waygroup> groups_;
    std::vector<PatrolPath> paths_;
    // The setup had a waypoint section (g_CurrentSetup.pathwaypoints != NULL)
    // and a group section.
    bool hasWaypoints_ = false;
    bool hasGroups_ = false;
};

} // namespace Frigate::Level
