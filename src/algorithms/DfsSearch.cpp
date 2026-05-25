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

    // Limit stanów DFS jest liczony osobno dla każdego przystanku startowego.
    // Dzięki temu przy wyszukiwaniu po nazwie pierwszy kandydat nie blokuje
    // pozostałych przystanków o tej samej nazwie.
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

        // Dominacja stanu: jeżeli byliśmy już na tym samym przystanku
        // nie później i przy użyciu nie większej liczby krawędzi, to obecny
        // stan nie może dać lepszego wyniku. Wcześniejsze przybycie na ten sam
        // przystanek zawsze może poczekać na te same kursy co późniejsze.
        for (int previousDepth = 0; previousDepth <= depth; ++previousDepth) {
            if (bestTimeByDepth[previousDepth][currentStop] <= currentTime) {
                return;
            }
        }
        bestTimeByDepth[depth][currentStop] = currentTime;

        ++currentStartVisitedStates;
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

            if (nextTime >= bestArrival) {
                continue;
            }

            // Proste zabezpieczenie przed cyklami typu A -> B -> A -> B.
            // Dla problemu najwcześniejszego przyjazdu takie cykle prawie nigdy nie są potrzebne.
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
        result.transfers = countTransfers(result.pathConnections, connections);
    }

    const auto finishedAt = Clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return result;
}
