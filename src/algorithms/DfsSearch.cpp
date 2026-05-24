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
    result.methodName = "DFS ograniczony";
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
    int bestStartStop = startStops.front();
    int bestTargetStop = targetStops.front();
    int currentStartStop = -1;

    std::vector<int> currentPath;
    std::vector<int> bestPath;
    std::vector<bool> stopInCurrentPath(stops.size(), false);

    std::function<void(int, int, int)> dfs = [&](int currentStop, int currentTime, int depth) {
        if (result.visitedStates >= maxVisitedStates) {
            result.stoppedByLimit = true;
            return;
        }

        ++result.visitedStates;

        if (isTarget[currentStop]) {
            if (currentTime < bestArrival) {
                bestArrival = currentTime;
                bestPath = currentPath;
                bestStartStop = currentStartStop;
                bestTargetStop = currentStop;
            }
            return;
        }

        if (depth >= maxDepth) {
            return;
        }

        if (currentTime >= bestArrival || currentTime > maxArrivalTime) {
            return;
        }

        const auto& outgoing = adjacency[currentStop];

        const auto firstUsable = std::lower_bound(
            outgoing.begin(),
            outgoing.end(),
            currentTime,
            [this](int connectionIndex, int time) {
                return connections[connectionIndex].departure < time;
            }
        );

        for (auto iterator = firstUsable; iterator != outgoing.end(); ++iterator) {
            if (result.visitedStates >= maxVisitedStates) {
                result.stoppedByLimit = true;
                return;
            }

            const int connectionIndex = *iterator;
            const Connection& connection = connections[connectionIndex];
            ++result.consideredEdges;

            if (connection.arrival > maxArrivalTime) {
                continue;
            }

            if (connection.arrival >= bestArrival) {
                continue;
            }

            // Proste zabezpieczenie przed cyklami typu A -> B -> A -> B.
            // Dla problemu najwcześniejszego przyjazdu takie cykle prawie nigdy nie są potrzebne.
            if (stopInCurrentPath[connection.to]) {
                continue;
            }

            currentPath.push_back(connectionIndex);
            stopInCurrentPath[connection.to] = true;

            dfs(connection.to, connection.arrival, depth + 1);

            stopInCurrentPath[connection.to] = false;
            currentPath.pop_back();
        }
    };

    for (int startStop : startStops) {
        if (result.stoppedByLimit) {
            break;
        }

        currentStartStop = startStop;
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
        result.transfers = countTransfers(result.pathConnections, connections);
    }

    const auto finishedAt = Clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return result;
}
