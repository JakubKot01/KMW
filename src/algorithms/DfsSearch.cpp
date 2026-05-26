#include "TransitGraph.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

SearchResult TransitGraph::findEarliestArrivalDfs(
    int startStop,
    int targetStop,
    int startTimeSeconds,
    int maxDepth,
    int maxDurationSeconds,
    int maxVisitedStates
) const {
    return findEarliestArrivalDfs(
        std::vector<int>{startStop},
        std::vector<int>{targetStop},
        startTimeSeconds,
        maxDepth,
        maxDurationSeconds,
        maxVisitedStates
    );
}

SearchResult TransitGraph::findEarliestArrivalDfs(
    const std::vector<int>& startStops,
    const std::vector<int>& targetStops,
    int startTimeSeconds,
    int maxDepth,
    int maxDurationSeconds,
    int maxVisitedStates
) const {
    if (startStops.empty()) {
        throw std::runtime_error("DFS: lista przystanków startowych jest pusta.");
    }
    if (targetStops.empty()) {
        throw std::runtime_error("DFS: lista przystanków docelowych jest pusta.");
    }

    for (int stopIndex : startStops) {
        validateStopIndex(stopIndex, "startStop");
    }
    for (int stopIndex : targetStops) {
        validateStopIndex(stopIndex, "targetStop");
    }

    using Clock = std::chrono::steady_clock;
    const auto startedAt = Clock::now();

    SearchResult result;
    result.methodName = "Bounded DFS";
    result.startStop = startStops.front();
    result.targetStop = targetStops.front();
    result.requestedStartTime = startTimeSeconds;
    result.maxDepth = maxDepth;
    result.maxDurationSeconds = maxDurationSeconds;

    std::vector<char> isTarget(stops.size(), false);
    for (int targetStop : targetStops) {
        isTarget[targetStop] = true;
    }

    const int maxArrivalTime = startTimeSeconds + maxDurationSeconds;
    bool bestFound = false;
    int bestArrival = -1;
    int bestTransfers = -1;
    int bestStartStop = startStops.front();
    int bestTargetStop = targetStops.front();
    int currentStartStop = -1;

    std::vector<int> currentPath;
    std::vector<int> bestPath;
    std::vector<bool> stopInCurrentPath(stops.size(), false);

    int currentStartVisitedStates = 0;
    bool currentStartStoppedByLimit = false;

    struct DfsLabel {
        int arrivalTime = 0;
        int transfers = 0;
        int lastTrip = -1;
        int depth = 0;
        bool active = true;
    };

    std::vector<std::vector<DfsLabel>> labelsByStop(stops.size());

    auto dominates = [](const DfsLabel& existing, const DfsLabel& candidate) {
        if (!existing.active) {
            return false;
        }

        // A state reached using more edges is not necessarily better, even if it is earlier,
        // because it has less remaining depth available. Therefore, we only let a state
        // dominate another one when it is no deeper, no later and no worse in transfers.
        if (existing.depth > candidate.depth) {
            return false;
        }

        if (existing.arrivalTime > candidate.arrivalTime) {
            return false;
        }

        if (existing.transfers < candidate.transfers) {
            return true;
        }

        // If the transfer count is identical, the last used trip matters. Continuing with
        // the same trip may avoid a transfer on the next transit edge, so different lastTrip
        // values must usually be kept as separate labels.
        if (existing.transfers == candidate.transfers
            && (existing.lastTrip == candidate.lastTrip || existing.lastTrip == -1)) {
            return true;
        }

        return false;
    };

    auto registerLabel = [&](int stop, int arrivalTime, int transfers, int lastTrip, int depth) {
        DfsLabel candidate;
        candidate.arrivalTime = arrivalTime;
        candidate.transfers = transfers;
        candidate.lastTrip = lastTrip;
        candidate.depth = depth;

        for (const DfsLabel& existing : labelsByStop[stop]) {
            if (dominates(existing, candidate)) {
                return false;
            }
        }

        for (DfsLabel& existing : labelsByStop[stop]) {
            if (dominates(candidate, existing)) {
                existing.active = false;
            }
        }

        labelsByStop[stop].erase(
            std::remove_if(
                labelsByStop[stop].begin(),
                labelsByStop[stop].end(),
                [](const DfsLabel& label) { return !label.active; }
            ),
            labelsByStop[stop].end()
        );

        labelsByStop[stop].push_back(candidate);
        return true;
    };

    std::function<void(int, int, int, int, int)> dfs = [&](
        int currentStop,
        int currentTime,
        int depth,
        int currentTransfers,
        int lastTrip
    ) {
        if (currentStartStoppedByLimit) {
            return;
        }

        if (depth > maxDepth) {
            return;
        }

        if (currentTime > maxArrivalTime) {
            return;
        }

        if (bestFound && currentTime > bestArrival) {
            return;
        }

        if (!registerLabel(currentStop, currentTime, currentTransfers, lastTrip, depth)) {
            return;
        }

        if (maxVisitedStates > 0 && currentStartVisitedStates >= maxVisitedStates) {
            currentStartStoppedByLimit = true;
            result.stoppedByLimit = true;
            return;
        }

        ++currentStartVisitedStates;
        ++result.visitedStates;

        if (isTarget[currentStop]) {
            if (!bestFound || currentTime < bestArrival
                || (currentTime == bestArrival && currentTransfers < bestTransfers)) {
                bestFound = true;
                bestArrival = currentTime;
                bestTransfers = currentTransfers;
                bestPath = currentPath;
                bestStartStop = currentStartStop;
                bestTargetStop = currentStop;
            }

            return;
        }

        if (depth >= maxDepth) {
            return;
        }

        const auto& outgoing = adjacency[currentStop];

        for (int connectionIndex : outgoing) {
            if (currentStartStoppedByLimit) {
                return;
            }

            const Connection& connection = connections[connectionIndex];
            ++result.consideredEdges;

            int nextTime = -1;
            int nextTransfers = currentTransfers;
            int nextLastTrip = lastTrip;

            if (connection.isWalking()) {
                nextTime = currentTime + connection.travelTime;
            } else {
                if (connection.departure < currentTime) {
                    continue;
                }

                nextTime = connection.arrival;

                if (lastTrip != -1 && lastTrip != connection.trip) {
                    ++nextTransfers;
                }

                nextLastTrip = connection.trip;
            }

            if (nextTime > maxArrivalTime) {
                continue;
            }

            if (bestFound && nextTime > bestArrival) {
                continue;
            }

            if (stopInCurrentPath[connection.to]) {
                continue;
            }

            currentPath.push_back(connectionIndex);
            stopInCurrentPath[connection.to] = true;
            dfs(connection.to, nextTime, depth + 1, nextTransfers, nextLastTrip);
            stopInCurrentPath[connection.to] = false;
            currentPath.pop_back();
        }
    };

    for (int startStop : startStops) {
        currentStartStop = startStop;
        currentStartVisitedStates = 0;
        currentStartStoppedByLimit = false;
        currentPath.clear();
        std::fill(stopInCurrentPath.begin(), stopInCurrentPath.end(), false);
        stopInCurrentPath[startStop] = true;

        for (auto& labels : labelsByStop) {
            labels.clear();
        }

        dfs(startStop, startTimeSeconds, 0, 0, -1);
    }

    if (bestFound) {
        result.found = true;
        result.startStop = bestStartStop;
        result.targetStop = bestTargetStop;
        result.arrivalTime = bestArrival;
        result.totalTravelTime = result.arrivalTime - startTimeSeconds;
        result.pathConnections = std::move(bestPath);
        result.transfers = bestTransfers;
    }

    const auto finishedAt = Clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return result;
}
