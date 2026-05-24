#include "TransitGraph.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

SearchResult TransitGraph::findEarliestArrivalDijkstra(
    int startStop,
    int targetStop,
    int startTimeSeconds
) const {
    return findEarliestArrivalDijkstra(
        std::vector<int>{startStop},
        std::vector<int>{targetStop},
        startTimeSeconds
    );
}

SearchResult TransitGraph::findEarliestArrivalDijkstra(
    const std::vector<int>& startStops,
    const std::vector<int>& targetStops,
    int startTimeSeconds
) const {
    if (startStops.empty()) {
        throw std::runtime_error("Dijkstra: lista przystanków startowych jest pusta.");
    }
    if (targetStops.empty()) {
        throw std::runtime_error("Dijkstra: lista przystanków docelowych jest pusta.");
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
    result.methodName = "Dijkstra czasowa";
    result.startStop = startStops.front();
    result.targetStop = targetStops.front();
    result.requestedStartTime = startTimeSeconds;

    std::vector<char> isTarget(stops.size(), false);
    for (int targetStop : targetStops) {
        isTarget[targetStop] = true;
    }

    const int infinity = std::numeric_limits<int>::max() / 4;
    std::vector<int> earliestArrival(stops.size(), infinity);
    std::vector<int> previousConnection(stops.size(), -1);
    std::vector<int> sourceForStop(stops.size(), -1);

    using QueueItem = std::pair<int, int>; // arrival time, stop index
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    for (int startStop : startStops) {
        if (startTimeSeconds < earliestArrival[startStop]) {
            earliestArrival[startStop] = startTimeSeconds;
            previousConnection[startStop] = -1;
            sourceForStop[startStop] = startStop;
            queue.push({startTimeSeconds, startStop});
        }
    }

    int bestTargetStop = -1;

    while (!queue.empty()) {
        const auto [currentTime, currentStop] = queue.top();
        queue.pop();

        if (currentTime != earliestArrival[currentStop]) {
            continue;
        }

        ++result.visitedStates;

        if (isTarget[currentStop]) {
            bestTargetStop = currentStop;
            break;
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
            const int connectionIndex = *iterator;
            const Connection& connection = connections[connectionIndex];
            ++result.consideredEdges;

            if (connection.arrival < earliestArrival[connection.to]) {
                earliestArrival[connection.to] = connection.arrival;
                previousConnection[connection.to] = connectionIndex;
                sourceForStop[connection.to] = sourceForStop[currentStop];
                queue.push({connection.arrival, connection.to});
            }
        }
    }

    if (bestTargetStop != -1) {
        result.found = true;
        result.targetStop = bestTargetStop;
        result.startStop = sourceForStop[bestTargetStop];
        result.arrivalTime = earliestArrival[bestTargetStop];
        result.totalTravelTime = result.arrivalTime - startTimeSeconds;

        int currentStop = bestTargetStop;
        while (currentStop != result.startStop) {
            const int connectionIndex = previousConnection[currentStop];
            if (connectionIndex < 0) {
                throw std::runtime_error("Nie można odtworzyć ścieżki Dijkstry mimo znalezionego celu.");
            }

            result.pathConnections.push_back(connectionIndex);
            currentStop = connections[connectionIndex].from;
        }

        std::reverse(result.pathConnections.begin(), result.pathConnections.end());
        result.transfers = countTransfers(result.pathConnections, connections);
    }

    const auto finishedAt = Clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return result;
}
