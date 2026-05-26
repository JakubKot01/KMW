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
    result.methodName = "Time-based Dijkstra";
    result.startStop = startStops.front();
    result.targetStop = targetStops.front();
    result.requestedStartTime = startTimeSeconds;

    std::vector<char> isTarget(stops.size(), false);
    for (int targetStop : targetStops) {
        isTarget[targetStop] = true;
    }

    struct Label {
        int stop = -1;
        int arrivalTime = 0;
        int transfers = 0;
        int lastTrip = -1;
        int previousLabel = -1;
        int previousConnection = -1;
        int sourceStop = -1;
        bool active = true;
    };

    struct QueueItem {
        int labelIndex = -1;
    };

    std::vector<Label> labels;
    std::vector<std::vector<int>> labelsByStop(stops.size());

    auto isDominatedBy = [&](const Label& existing, const Label& candidate) {
        if (!existing.active) {
            return false;
        }

        if (existing.arrivalTime > candidate.arrivalTime) {
            return false;
        }

        if (existing.transfers < candidate.transfers) {
            return true;
        }

        if (existing.transfers == candidate.transfers && existing.lastTrip == candidate.lastTrip) {
            return true;
        }

        return false;
    };

    auto dominates = [&](const Label& candidate, const Label& existing) {
        if (!existing.active) {
            return false;
        }

        if (candidate.arrivalTime > existing.arrivalTime) {
            return false;
        }

        if (candidate.transfers < existing.transfers) {
            return true;
        }

        if (candidate.transfers == existing.transfers && candidate.lastTrip == existing.lastTrip) {
            return true;
        }

        return false;
    };

    auto addLabel = [&](const Label& candidate) -> int {
        for (int labelIndex : labelsByStop[candidate.stop]) {
            if (isDominatedBy(labels[labelIndex], candidate)) {
                return -1;
            }
        }

        const int newIndex = static_cast<int>(labels.size());
        labels.push_back(candidate);

        for (int labelIndex : labelsByStop[candidate.stop]) {
            if (dominates(candidate, labels[labelIndex])) {
                labels[labelIndex].active = false;
            }
        }

        labelsByStop[candidate.stop].push_back(newIndex);
        return newIndex;
    };

    struct QueueItemGreater {
        const std::vector<Label>* labels = nullptr;

        bool operator()(const QueueItem& left, const QueueItem& right) const {
            const Label& leftLabel = (*labels)[left.labelIndex];
            const Label& rightLabel = (*labels)[right.labelIndex];

            if (leftLabel.arrivalTime != rightLabel.arrivalTime) {
                return leftLabel.arrivalTime > rightLabel.arrivalTime;
            }

            if (leftLabel.transfers != rightLabel.transfers) {
                return leftLabel.transfers > rightLabel.transfers;
            }

            return leftLabel.stop > rightLabel.stop;
        }
    };

    QueueItemGreater comparator;
    comparator.labels = &labels;

    std::priority_queue<QueueItem, std::vector<QueueItem>, QueueItemGreater> queue(comparator);

    for (int startStop : startStops) {
        Label startLabel;
        startLabel.stop = startStop;
        startLabel.arrivalTime = startTimeSeconds;
        startLabel.transfers = 0;
        startLabel.lastTrip = -1;
        startLabel.previousLabel = -1;
        startLabel.previousConnection = -1;
        startLabel.sourceStop = startStop;

        const int labelIndex = addLabel(startLabel);
        if (labelIndex != -1) {
            queue.push({labelIndex});
        }
    }

    int bestTargetLabel = -1;

    while (!queue.empty()) {
        const QueueItem item = queue.top();
        queue.pop();

        const int currentLabelIndex = item.labelIndex;
        const Label currentLabel = labels[currentLabelIndex];

        if (!labels[currentLabelIndex].active) {
            continue;
        }

        ++result.visitedStates;

        if (isTarget[currentLabel.stop]) {
            bestTargetLabel = currentLabelIndex;
            break;
        }

        const auto& outgoing = adjacency[currentLabel.stop];

        for (int connectionIndex : outgoing) {
            const Connection& connection = connections[connectionIndex];
            ++result.consideredEdges;

            Label nextLabel;
            nextLabel.stop = connection.to;
            nextLabel.transfers = currentLabel.transfers;
            nextLabel.lastTrip = currentLabel.lastTrip;
            nextLabel.previousLabel = currentLabelIndex;
            nextLabel.previousConnection = connectionIndex;
            nextLabel.sourceStop = currentLabel.sourceStop;

            if (connection.isWalking()) {
                nextLabel.arrivalTime = currentLabel.arrivalTime + connection.travelTime;
            } else {
                if (connection.departure < currentLabel.arrivalTime) {
                    continue;
                }

                nextLabel.arrivalTime = connection.arrival;

                if (currentLabel.lastTrip != -1 && currentLabel.lastTrip != connection.trip) {
                    ++nextLabel.transfers;
                }

                nextLabel.lastTrip = connection.trip;
            }

            const int nextLabelIndex = addLabel(nextLabel);
            if (nextLabelIndex != -1) {
                queue.push({nextLabelIndex});
            }
        }
    }

    if (bestTargetLabel != -1) {
        const Label& targetLabel = labels[bestTargetLabel];

        result.found = true;
        result.targetStop = targetLabel.stop;
        result.startStop = targetLabel.sourceStop;
        result.arrivalTime = targetLabel.arrivalTime;
        result.totalTravelTime = result.arrivalTime - startTimeSeconds;
        result.transfers = targetLabel.transfers;

        int currentLabelIndex = bestTargetLabel;
        while (labels[currentLabelIndex].previousLabel != -1) {
            const int connectionIndex = labels[currentLabelIndex].previousConnection;
            result.pathConnections.push_back(connectionIndex);
            currentLabelIndex = labels[currentLabelIndex].previousLabel;
        }

        std::reverse(result.pathConnections.begin(), result.pathConnections.end());
    }

    const auto finishedAt = Clock::now();
    result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finishedAt - startedAt).count();

    return result;
}
