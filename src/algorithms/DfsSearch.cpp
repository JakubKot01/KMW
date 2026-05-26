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
    int bestArrival = std::numeric_limits<int>::max() / 4;
    int bestTransfers = std::numeric_limits<int>::max() / 4;
    int bestStartStop = startStops.front();
    int bestTargetStop = targetStops.front();
    int currentStartStop = -1;

    std::vector<int> currentPath;
    std::vector<int> bestPath;
    std::vector<bool> stopInCurrentPath(stops.size(), false);

    int currentStartVisitedStates = 0;
    bool currentStartStoppedByLimit = false;

    const int infinity = std::numeric_limits<int>::max() / 4;
    std::vector<std::vector<int>> bestTimeByDepth(
        maxDepth + 1,
        std::vector<int>(stops.size(), infinity)
    );

    std::function<void(int, int, int)> dfs = [&](int currentStop, int currentTime, int depth) {
        if (currentStartStoppedByLimit) {
            return;
        }

        if (currentStartVisitedStates >= maxVisitedStates) {
            currentStartStoppedByLimit = true;
            result.stoppedByLimit = true;
            return;
        }

        /*
            State domination: 
            If particular stop was already reached at the same or earlier time and with the same or smaller depth, 
            then current state cannot lead to a better solution. 
            Arriving later to the same stop almost never can be beneficial, 
            because you can always wait at that stop for the same connections as with earlier arrival.
        */
        for (int previousDepth = 0; previousDepth <= depth; ++previousDepth) {
            if (bestTimeByDepth[previousDepth][currentStop] <= currentTime) {
                return;
            }
        }
        bestTimeByDepth[depth][currentStop] = currentTime;

        ++currentStartVisitedStates;
        ++result.visitedStates;

        if (isTarget[currentStop]) {
            const int currentTransfers = countTransfers(currentPath, connections);

            if (currentTime < bestArrival
                || (currentTime == bestArrival && currentTransfers < bestTransfers)) {
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

        if (currentTime > bestArrival || currentTime > maxArrivalTime) {
            return;
        }

        const auto& outgoing = adjacency[currentStop];

        for (int connectionIndex : outgoing) {
            if (currentStartStoppedByLimit) {
                return;
            }
        
            if (currentStartVisitedStates >= maxVisitedStates) {
                currentStartStoppedByLimit = true;
                result.stoppedByLimit = true;
                return;
            }
        
            const Connection& connection = connections[connectionIndex];
            ++result.consideredEdges;

            int nextTime = -1;
            if (connection.isWalking()) {
                nextTime = currentTime + connection.travelTime;
            } else {
                if (connection.departure < currentTime) {
                    continue;
                }
                nextTime = connection.arrival;
            }

            if (nextTime > maxArrivalTime) {
                continue;
            }

            if (nextTime > bestArrival) {
                continue;
            }

            // A -> B -> A -> B cycle prevention
            if (stopInCurrentPath[connection.to]) {
                continue;
            }

            currentPath.push_back(connectionIndex);
            stopInCurrentPath[connection.to] = true;

            dfs(connection.to, nextTime, depth + 1);

            stopInCurrentPath[connection.to] = false;
            currentPath.pop_back();
        }
    };

    for (int startStop : startStops) {
        currentStartStop = startStop;
        currentStartVisitedStates = 0;
        currentStartStoppedByLimit = false;
        for (auto& row : bestTimeByDepth) {
            std::fill(row.begin(), row.end(), infinity);
        }
        currentPath.clear();
        std::fill(stopInCurrentPath.begin(), stopInCurrentPath.end(), false);
        stopInCurrentPath[startStop] = true;
        
        dfs(startStop, startTimeSeconds, 0);
    }

    if (bestArrival != std::numeric_limits<int>::max() / 4) {
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
