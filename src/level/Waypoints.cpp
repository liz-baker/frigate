#include "level/Waypoints.h"

#include <algorithm>
#include <array>
#include <sstream>

#include "level/StageSetup.h"
#include "model/Random.h"

namespace Frigate::Level {

namespace {

// waygroupFindRoute and waypointFindRoute mark the waypoints on a route by
// adding this to their distance.
constexpr int kRouteMark = 10000;

// Appends `format` with each "{}" replaced by the next argument.
void Fill(std::ostringstream& out, const char* format) {
    out << format;
}
template <class First, class... Rest>
void Fill(std::ostringstream& out, const char* format, const First& first, const Rest&... rest) {
    for (; *format != '\0'; format++) {
        if (format[0] == '{' && format[1] == '}') {
            out << first;
            Fill(out, format + 2, rest...);
            return;
        }
        out << *format;
    }
}
template <class... Args> void Report(std::vector<std::string>* errors, const char* format, const Args&... args) {
    std::ostringstream out;
    Fill(out, format, args...);
    errors->push_back(out.str());
}

} // namespace

void Waypoints::Reset(const StageSetup* setup) {
    setup_ = setup;
    waypoints_ = setup->PathWaypoints();
    groups_ = setup->WaypointGroups();
    paths_ = setup->PatrolPaths();
    hasWaypoints_ = setup->HasWaypointSection();
    hasGroups_ = setup->HasWaygroupSection();
}

int Waypoints::FindPath(int id) const {
    for (size_t i = 0; i < paths_.size(); i++) {
        if (paths_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Pad& Waypoints::PadOf(int waypoint) const {
    return setup_->Pads()[static_cast<size_t>(waypoints_[static_cast<size_t>(waypoint)].padId)];
}

int Waypoints::InitLinks() {
    int errors = 0;
    errors_.clear();

    if (hasWaypoints_) {
        for (size_t index = 0; index < waypoints_.size(); index++) {
            for (const int32_t neighbour : waypoints_[index].neighbours) {
                if (neighbour == static_cast<int32_t>(index)) {
                    Report(&errors_, "loc number {} has a link to itself", index);
                    errors++;
                } else if (!ValidPoint(neighbour)) {
                    Report(&errors_, "loc number {} links to {}, which does not exist", index, neighbour);
                    errors++;
                } else {
                    const auto& back = waypoints_[static_cast<size_t>(neighbour)].neighbours;
                    if (std::find(back.begin(), back.end(), static_cast<int32_t>(index)) == back.end()) {
                        Report(&errors_, "loc number {} has link to number {} but not back again", index, neighbour);
                        errors++;
                    }
                }
            }
        }
    }

    if (hasGroups_) {
        for (size_t index = 0; index < groups_.size(); index++) {
            for (const int32_t neighbour : groups_[index].neighbours) {
                if (neighbour == static_cast<int32_t>(index)) {
                    Report(&errors_, "hall number {} has a link to itself", index);
                    errors++;
                } else if (!ValidGroup(neighbour)) {
                    Report(&errors_, "hall number {} links to {}, which does not exist", index, neighbour);
                    errors++;
                } else {
                    const auto& back = groups_[static_cast<size_t>(neighbour)].neighbours;
                    if (std::find(back.begin(), back.end(), static_cast<int32_t>(index)) == back.end()) {
                        Report(&errors_, "hall number {} has link to number {} but not connected locs", index,
                               neighbour);
                        errors++;
                    } else if (hasWaypoints_) {
                        int pointA = -1;
                        int pointB = -1;
                        FindLink(static_cast<int>(index), neighbour, &pointA, &pointB);
                        if (pointA < 0 || pointB < 0) {
                            Report(&errors_, "hall number {} has link to number {} but not back again", index,
                                   neighbour);
                            errors++;
                        }
                    }
                }
            }
        }
    }

    if (hasWaypoints_ && hasGroups_) {
        // Each group numbers its waypoints; one that already has another
        // group's number is in two halls.
        for (size_t group = 0; group < groups_.size(); group++) {
            for (const int32_t member : groups_[group].waypoints) {
                if (!ValidPoint(member)) {
                    Report(&errors_, "hall number {} contains loc {}, which does not exist", group, member);
                    errors++;
                    continue;
                }
                Waypoint& point = waypoints_[static_cast<size_t>(member)];
                if (point.groupNum < 0) {
                    point.groupNum = static_cast<int32_t>(group);
                } else if (point.groupNum != static_cast<int32_t>(group)) {
                    Report(&errors_, "hall number {} contains loc number {} which thinks it is in hall number {}",
                           group, member, point.groupNum);
                    errors++;
                }
            }
        }

        for (size_t index = 0; index < waypoints_.size(); index++) {
            if (waypoints_[index].groupNum < 0) {
                Report(&errors_, "loc number {} is not in a hall", index);
                errors++;
            }
        }

        // Every waypoint of a group must be reachable from its first. The
        // fill marks reached waypoints with distance 1 (and, as the game's
        // does, any neighbour outside the group).
        for (size_t group = 0; group < groups_.size(); group++) {
            const std::vector<int32_t>& members = groups_[group].waypoints;
            for (size_t i = 0; i < members.size(); i++) {
                if (ValidPoint(members[i])) {
                    waypoints_[static_cast<size_t>(members[i])].dist = i == 0 ? 1 : 0;
                }
            }
            for (;;) {
                bool changed = false;
                bool disconnected = false;
                for (const int32_t member : members) {
                    if (!ValidPoint(member) || waypoints_[static_cast<size_t>(member)].dist != 1) {
                        continue;
                    }
                    for (const int32_t neighbour : waypoints_[static_cast<size_t>(member)].neighbours) {
                        if (ValidPoint(neighbour) && waypoints_[static_cast<size_t>(neighbour)].dist != 1) {
                            waypoints_[static_cast<size_t>(neighbour)].dist = 1;
                            changed = true;
                        }
                    }
                }
                for (const int32_t member : members) {
                    if (ValidPoint(member) && waypoints_[static_cast<size_t>(member)].dist != 1) {
                        disconnected = true;
                        break;
                    }
                }
                if (!changed || !disconnected) {
                    if (disconnected) {
                        Report(&errors_, "not all locs in hall number {} are connected", group);
                        errors++;
                    }
                    break;
                }
            }
        }
    }

    return errors;
}

int Waypoints::FindOnTile(int tile) const {
    if (!hasWaypoints_) {
        return -1;
    }
    for (size_t i = 0; i < waypoints_.size(); i++) {
        if (PadOf(static_cast<int>(i)).stanTile == tile) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float Waypoints::PlaneDistanceSquared(const Vec3& position, int waypoint) const {
    const Pad& pad = PadOf(waypoint);
    const float dx = pad.position[0] - position[0];
    const float dz = pad.position[2] - position[2];
    return (dx * dx) + (dz * dz);
}

int Waypoints::NearestTo(Stan& stan, const Vec3& position, int tile) const {
    const int found = stan.FillSearch(tile, [this](int candidate) { return FindOnTile(candidate) >= 0; });
    if (found == kNoTile) {
        return -1;
    }
    int result = FindOnTile(found);
    if (result >= 0) {
        const float best = PlaneDistanceSquared(position, result);
        for (const int32_t neighbour : waypoints_[static_cast<size_t>(result)].neighbours) {
            if (PlaneDistanceSquared(position, neighbour) < best) {
                result = neighbour;
            }
        }
    }
    return result;
}

int Waypoints::FindNearestToPosition(const Vec3& position) const {
    int result = -1;
    if (hasWaypoints_) {
        float compare = -1.0f;
        for (size_t i = 0; i < waypoints_.size(); i++) {
            const Pad& pad = PadOf(static_cast<int>(i));
            const float distance = ((position[2] - pad.position[2]) * (position[2] - pad.position[2])) +
                                   ((position[0] - pad.position[0]) * (position[0] - pad.position[0]));
            if (compare < 0.0f || distance < compare) {
                compare = distance;
                result = static_cast<int>(i);
            }
        }
    }
    return result;
}

int Waypoints::RefineNearest(const Vec3& position, int waypoint) const {
    int result = waypoint;
    const Pad& pad = PadOf(waypoint);
    float distance = ((position[2] - pad.position[2]) * (position[2] - pad.position[2])) +
                     ((position[0] - pad.position[0]) * (position[0] - pad.position[0]));
    for (const int32_t neighbour : waypoints_[static_cast<size_t>(waypoint)].neighbours) {
        const Pad& other = PadOf(neighbour);
        const float candidate = ((position[2] - other.position[2]) * (position[2] - other.position[2])) +
                                ((position[0] - other.position[0]) * (position[0] - other.position[0]));
        if (candidate < distance) {
            distance = candidate;
            result = neighbour;
        }
    }
    return result;
}

int Waypoints::FindGroupByDist(const std::vector<int32_t>& groups, int dist) const {
    for (const int32_t group : groups) {
        if (ValidGroup(group) && groups_[static_cast<size_t>(group)].dist == dist) {
            return group;
        }
    }
    return -1;
}

void Waypoints::SetGroupsUnvisited(const std::vector<int32_t>& groups, int value) {
    for (const int32_t group : groups) {
        if (ValidGroup(group) && groups_[static_cast<size_t>(group)].dist < 0) {
            groups_[static_cast<size_t>(group)].dist = value;
        }
    }
}

bool Waypoints::PropagateGroupDist(int arg) {
    bool result = false;
    for (const Waygroup& group : groups_) {
        if (group.dist == arg) {
            result = true;
            SetGroupsUnvisited(group.neighbours, arg + 1);
        }
    }
    return result;
}

bool Waypoints::FloodGroupDist(int from, int to, bool everything) {
    for (Waygroup& group : groups_) {
        group.dist = -1;
    }
    groups_[static_cast<size_t>(from)].dist = 0;

    bool result = true;
    for (int i = 0; (everything || groups_[static_cast<size_t>(to)].dist < 0) && result; i++) {
        result = PropagateGroupDist(i);
    }
    return result;
}

bool Waypoints::MarkGroupRoute(int from, int to) {
    const bool result = FloodGroupDist(from, to, false);
    if (result) {
        int current = to;
        int i = groups_[static_cast<size_t>(current)].dist - 1;
        while (i >= 0) {
            groups_[static_cast<size_t>(current)].dist += kRouteMark;
            current = FindGroupByDist(groups_[static_cast<size_t>(current)].neighbours, i);
            if (current < 0) {
                return result;
            }
            i--;
        }
        groups_[static_cast<size_t>(current)].dist += kRouteMark;
    }
    return result;
}

int Waypoints::FindPointByDistInGroup(const std::vector<int32_t>& points, int dist, int group) const {
    for (const int32_t point : points) {
        if (ValidPoint(point) && waypoints_[static_cast<size_t>(point)].groupNum == group &&
            waypoints_[static_cast<size_t>(point)].dist == dist) {
            return point;
        }
    }
    return -1;
}

void Waypoints::SetPointsUnvisited(const std::vector<int32_t>& points, int value, int group) {
    for (const int32_t point : points) {
        if (ValidPoint(point) && waypoints_[static_cast<size_t>(point)].groupNum == group &&
            waypoints_[static_cast<size_t>(point)].dist < 0) {
            waypoints_[static_cast<size_t>(point)].dist = value;
        }
    }
}

bool Waypoints::PropagatePointDist(const std::vector<int32_t>& points, int arg, int group) {
    bool result = false;
    for (const int32_t point : points) {
        if (ValidPoint(point) && waypoints_[static_cast<size_t>(point)].dist == arg) {
            result = true;
            SetPointsUnvisited(waypoints_[static_cast<size_t>(point)].neighbours, arg + 1, group);
        }
    }
    return result;
}

void Waypoints::Bfs(int from, int to, bool everything) {
    const int group = waypoints_[static_cast<size_t>(from)].groupNum;
    const std::vector<int32_t>& members = groups_[static_cast<size_t>(group)].waypoints;
    for (const int32_t member : members) {
        waypoints_[static_cast<size_t>(member)].dist = -1;
    }
    waypoints_[static_cast<size_t>(from)].dist = 0;

    bool more = true;
    for (int i = 0; (everything || waypoints_[static_cast<size_t>(to)].dist < 0) && more; i++) {
        more = PropagatePointDist(members, i, group);
    }
}

void Waypoints::MarkRoute(int from, int to) {
    Bfs(from, to, false);
    int value = waypoints_[static_cast<size_t>(to)].dist - 1;
    int current = to;
    const int group = waypoints_[static_cast<size_t>(from)].groupNum;
    while (value >= 0) {
        waypoints_[static_cast<size_t>(current)].dist += kRouteMark;
        current = FindPointByDistInGroup(waypoints_[static_cast<size_t>(current)].neighbours, value, group);
        if (current < 0) {
            return;
        }
        value--;
    }
    waypoints_[static_cast<size_t>(current)].dist += kRouteMark;
}

int Waypoints::FindRouteInGroup(int from, int to, int* route, int capacity) {
    int* cursor = route;
    if (capacity >= 2) {
        MarkRoute(from, to);

        *cursor++ = from;
        int current = from;
        capacity += 9999;
        const int group = waypoints_[static_cast<size_t>(from)].groupNum;
        for (int i = 10001; i <= waypoints_[static_cast<size_t>(to)].dist && i < capacity; i++) {
            current = FindPointByDistInGroup(waypoints_[static_cast<size_t>(current)].neighbours, i, group);
            *cursor++ = current;
            if (current < 0) {
                break;
            }
        }
    }
    *cursor++ = -1;
    return static_cast<int>(cursor - route);
}

void Waypoints::FindLink(int groupA, int groupB, int* pointA, int* pointB) const {
    for (const int32_t member : groups_[static_cast<size_t>(groupA)].waypoints) {
        for (const int32_t neighbour : waypoints_[static_cast<size_t>(member)].neighbours) {
            if (waypoints_[static_cast<size_t>(neighbour)].groupNum == groupB) {
                *pointA = member;
                *pointB = neighbour;
                return;
            }
        }
    }
    *pointB = -1;
    *pointA = -1;
}

int Waypoints::FindRoute(int from, int to, int* route, int capacity) {
    int* cursor = route;
    if (hasGroups_) {
        const int fromGroup = waypoints_[static_cast<size_t>(from)].groupNum;
        const int toGroup = waypoints_[static_cast<size_t>(to)].groupNum;
        if (ValidGroup(fromGroup) && ValidGroup(toGroup) && MarkGroupRoute(fromGroup, toGroup)) {
            int currentPoint = from;
            int currentGroup = fromGroup;
            for (int i = groups_[static_cast<size_t>(fromGroup)].dist + 1;
                 i <= groups_[static_cast<size_t>(toGroup)].dist && capacity >= 2; i++) {
                const int nextGroup = FindGroupByDist(groups_[static_cast<size_t>(currentGroup)].neighbours, i);
                int exitPoint = -1;
                int nextPoint = -1;
                if (nextGroup >= 0) {
                    FindLink(currentGroup, nextGroup, &exitPoint, &nextPoint);
                }
                if (exitPoint < 0 || nextPoint < 0) {
                    break;
                }
                const int written = FindRouteInGroup(currentPoint, exitPoint, cursor, capacity) - 1;
                capacity -= written;
                cursor += written;
                currentPoint = nextPoint;
                currentGroup = nextGroup;
            }
            cursor += FindRouteInGroup(currentPoint, to, cursor, capacity) - 1;
        }
    }
    *cursor++ = -1;
    return static_cast<int>(cursor - route);
}

void Waypoints::ResetDistances() {
    for (Waypoint& point : waypoints_) {
        point.dist = -1;
    }
}

int Waypoints::FindPointRandomByDist(const std::vector<int32_t>& points, int dist) {
    const int length = static_cast<int>(points.size());
    if (length == 0) {
        return -1;
    }
    // Both halves of the list are scanned so that any waypoint meeting the
    // distance is equally likely.
    const int start = static_cast<int>(RandomGetNext() % static_cast<uint32_t>(length));
    for (int i = start; i < length; i++) {
        if (waypoints_[static_cast<size_t>(points[static_cast<size_t>(i)])].dist == dist) {
            return points[static_cast<size_t>(i)];
        }
    }
    for (int i = 0; i < start; i++) {
        if (waypoints_[static_cast<size_t>(points[static_cast<size_t>(i)])].dist == dist) {
            return points[static_cast<size_t>(i)];
        }
    }
    return -1;
}

int Waypoints::FindGroupRandomByDist(const std::vector<int32_t>& groups, int dist) {
    const int length = static_cast<int>(groups.size());
    if (length == 0) {
        return -1;
    }
    const int start = static_cast<int>(RandomGetNext() % static_cast<uint32_t>(length));
    for (int i = start; i < length; i++) {
        if (groups_[static_cast<size_t>(groups[static_cast<size_t>(i)])].dist == dist) {
            return groups[static_cast<size_t>(i)];
        }
    }
    for (int i = 0; i < start; i++) {
        if (groups_[static_cast<size_t>(groups[static_cast<size_t>(i)])].dist == dist) {
            return groups[static_cast<size_t>(i)];
        }
    }
    return -1;
}

int Waypoints::FindNextStepToward(int from, int to) {
    if (!hasGroups_) {
        return -1;
    }
    const int groupA = waypoints_[static_cast<size_t>(from)].groupNum;
    const int groupB = waypoints_[static_cast<size_t>(to)].groupNum;
    if (!ValidGroup(groupA) || !ValidGroup(groupB)) {
        return -1;
    }

    if (groupA == groupB) {
        ResetDistances();
        Bfs(to, from, true);

        int result = FindPointRandomByDist(waypoints_[static_cast<size_t>(from)].neighbours, -1);
        if (result >= 0) {
            return result;
        }
        result = FindPointRandomByDist(waypoints_[static_cast<size_t>(from)].neighbours,
                                       waypoints_[static_cast<size_t>(from)].dist + 1);
        if (result >= 0) {
            return result;
        }
        return -1;
    }

    FloodGroupDist(groupB, groupA, false);
    if (groups_[static_cast<size_t>(groupA)].dist >= 0) {
        const int chosen = FindGroupRandomByDist(groups_[static_cast<size_t>(groupA)].neighbours, -1);
        if (chosen >= 0) {
            int exitPoint = -1;
            int entryPoint = -1;
            FindLink(groupA, chosen, &exitPoint, &entryPoint);
            if (exitPoint == from) {
                return entryPoint;
            }
            if (exitPoint >= 0) {
                std::array<int, 3> route{};
                if (FindRouteInGroup(from, exitPoint, route.data(), 3) >= 3) {
                    return route[1];
                }
            }
        } else {
            const int towards = FindGroupByDist(groups_[static_cast<size_t>(groupA)].neighbours,
                                                groups_[static_cast<size_t>(groupA)].dist - 1);
            if (towards >= 0) {
                int exitPoint = -1;
                int entryPoint = -1;
                FindLink(groupA, towards, &exitPoint, &entryPoint);
                if (exitPoint >= 0) {
                    Bfs(exitPoint, from, true);
                    const int result = FindPointByDistInGroup(waypoints_[static_cast<size_t>(from)].neighbours,
                                                              waypoints_[static_cast<size_t>(from)].dist + 1,
                                                              waypoints_[static_cast<size_t>(from)].groupNum);
                    if (result >= 0) {
                        return result;
                    }
                }
            }
        }
    }
    return -1;
}

} // namespace Frigate::Level
