#pragma once

#include <optional>
#include <string>
#include <vector>

struct Stop {
    int index = -1;
    std::string id;
    std::string code;
    std::string name;
    double lat = 0.0;
    double lon = 0.0;
    std::string platformCode;
    std::optional<int> wheelchairBoarding;
};

struct Route {
    int index = -1;
    std::string id;
    std::string agencyId;
    std::string shortName;
    std::string longName;
    std::string description;
    int type = -1;
};

struct Trip {
    int index = -1;
    std::string id;
    int routeIndex = -1;
    std::string routeId;
    std::string serviceId;
    std::string headsign;
    std::optional<int> directionId;
    std::string shapeId;
};

enum class ConnectionType {
    Transit,
    Walk
};

struct Connection {
    int from = -1;
    int to = -1;
    int route = -1;
    int trip = -1;
    int departure = 0;
    int arrival = 0;
    int travelTime = 0;
    int fromSequence = -1;
    int toSequence = -1;
    ConnectionType type = ConnectionType::Transit;
    double distanceMeters = 0.0;

    bool isWalking() const {
        return type == ConnectionType::Walk;
    }
};

struct SearchResult {
    std::string methodName;
    bool found = false;

    int startStop = -1;
    int targetStop = -1;
    int requestedStartTime = 0;
    int arrivalTime = -1;
    int totalTravelTime = -1;
    int transfers = 0;

    int visitedStates = 0;
    int consideredEdges = 0;
    int maxDepth = 0;
    int maxDurationSeconds = 0;
    bool stoppedByLimit = false;
    double elapsedMilliseconds = 0.0;

    std::vector<int> pathConnections;
};

class TransitGraph {
public:
    static TransitGraph loadFromJson(const std::string& path);

    void buildAdjacency();
    int addWalkingConnections(
        double maxWalkingDistanceMeters,
        double walkingSpeedMetersPerSecond,
        int walkingPenaltySeconds
    );

    const std::vector<Stop>& getStops() const;
    const std::vector<Route>& getRoutes() const;
    const std::vector<Trip>& getTrips() const;
    const std::vector<Connection>& getConnections() const;
    const std::vector<std::vector<int>>& getAdjacency() const;

    const std::vector<int>& outgoingConnections(int stopIndex) const;

    std::vector<int> findStopsByName(const std::string& text) const;
    std::vector<int> findStopsByExactName(const std::string& name) const;

    SearchResult findEarliestArrivalDijkstra(int startStop, int targetStop, int startTimeSeconds) const;
    SearchResult findEarliestArrivalDijkstra(
        const std::vector<int>& startStops,
        const std::vector<int>& targetStops,
        int startTimeSeconds
    ) const;

    SearchResult findEarliestArrivalDfs(
        int startStop,
        int targetStop,
        int startTimeSeconds,
        int maxDepth,
        int maxDurationSeconds,
        int maxVisitedStates
    ) const;
    SearchResult findEarliestArrivalDfs(
        const std::vector<int>& startStops,
        const std::vector<int>& targetStops,
        int startTimeSeconds,
        int maxDepth,
        int maxDurationSeconds,
        int maxVisitedStates
    ) const;

    void printStats() const;
    void printStopsByName(const std::string& text, int maxResults = 20) const;
    void printGraphSample(int maxStops = 10, int maxEdgesPerStop = 8) const;
    void printStopNeighborhood(int stopIndex, int maxEdges = 30) const;
    void printSearchResult(const SearchResult& result, bool printPath = true) const;
    void compareAlgorithms(
        int startStop,
        int targetStop,
        int startTimeSeconds,
        int dfsMaxDepth,
        int dfsMaxDurationSeconds,
        int dfsMaxVisitedStates
    ) const;
    void compareAlgorithmsByNames(
        const std::string& startStopName,
        const std::string& targetStopName,
        int startTimeSeconds,
        int dfsMaxDepth,
        int dfsMaxDurationSeconds,
        int dfsMaxVisitedStates
    ) const;

    void exportDot(const std::string& path, int maxStops = 50, int maxEdgesPerStop = 4) const;

private:
    std::vector<Stop> stops;
    std::vector<Route> routes;
    std::vector<Trip> trips;
    std::vector<Connection> connections;
    std::vector<std::vector<int>> adjacency;

    static std::string secondsToTime(int seconds);
    static std::string durationToText(int seconds);
    static std::string toLower(std::string value);
    static std::string escapeDot(std::string value);
    static std::string routeLabel(const Route& route);
    static int countTransfers(const std::vector<int>& pathConnections, const std::vector<Connection>& connections);

    const Stop& stopAt(int index) const;
    const Route& routeAt(int index) const;
    const Trip& tripAt(int index) const;
    void validateStopIndex(int stopIndex, const std::string& label) const;
};
