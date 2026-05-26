#include "TransitGraph.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <cctype>
#include <cmath>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
    std::string normalizeText(const std::string& text) {
        std::string result;
        result.reserve(text.size());

        bool previousWasSpace = true;

        const auto appendNormalizedCharacter = [&result, &previousWasSpace](char character) {
            const unsigned char c = static_cast<unsigned char>(character);

            if (std::isspace(c)) {
                if (!previousWasSpace) {
                    result += ' ';
                    previousWasSpace = true;
                }
                return;
            }

            result += static_cast<char>(std::tolower(c));
            previousWasSpace = false;
        };

        for (size_t i = 0; i < text.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);

            // ASCII
            if (c < 0x80) {
                appendNormalizedCharacter(static_cast<char>(c));
                continue;
            }

            // Polskie znaki w UTF-8.
            // Dzięki temu użytkownik może pisać np. "Dworzec Glowny",
            // a program dopasuje przystanek zapisany jako "Dworzec Główny".
            if (i + 1 < text.size()) {
                const unsigned char next = static_cast<unsigned char>(text[i + 1]);

                if (c == 0xC3) {
                    switch (next) {
                        case 0xB3: appendNormalizedCharacter('o'); ++i; continue; // ó
                        case 0x93: appendNormalizedCharacter('o'); ++i; continue; // Ó
                    }
                }

                if (c == 0xC4) {
                    switch (next) {
                        case 0x85: appendNormalizedCharacter('a'); ++i; continue; // ą
                        case 0x84: appendNormalizedCharacter('a'); ++i; continue; // Ą
                        case 0x87: appendNormalizedCharacter('c'); ++i; continue; // ć
                        case 0x86: appendNormalizedCharacter('c'); ++i; continue; // Ć
                        case 0x99: appendNormalizedCharacter('e'); ++i; continue; // ę
                        case 0x98: appendNormalizedCharacter('e'); ++i; continue; // Ę
                    }
                }

                if (c == 0xC5) {
                    switch (next) {
                        case 0x82: appendNormalizedCharacter('l'); ++i; continue; // ł
                        case 0x81: appendNormalizedCharacter('l'); ++i; continue; // Ł
                        case 0x84: appendNormalizedCharacter('n'); ++i; continue; // ń
                        case 0x83: appendNormalizedCharacter('n'); ++i; continue; // Ń
                        case 0x9B: appendNormalizedCharacter('s'); ++i; continue; // ś
                        case 0x9A: appendNormalizedCharacter('s'); ++i; continue; // Ś
                        case 0xBA: appendNormalizedCharacter('z'); ++i; continue; // ź
                        case 0xB9: appendNormalizedCharacter('z'); ++i; continue; // Ź
                        case 0xBC: appendNormalizedCharacter('z'); ++i; continue; // ż
                        case 0xBB: appendNormalizedCharacter('z'); ++i; continue; // Ż
                    }
                }
            }

            // Nieznany znak spoza ASCII - pomijamy go w porównaniu nazw.
            // To zwiększa odporność na problemy z kodowaniem terminala.
        }

        if (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }

        return result;
    }

    template <typename T>
    T requiredValue(const json& item, const char* key) {
        if (!item.contains(key)) {
            throw std::runtime_error(std::string("Brak wymaganego pola JSON: ") + key);
        }
        return item.at(key).get<T>();
    }

    std::optional<int> optionalInt(const json& item, const char* key) {
        if (!item.contains(key) || item.at(key).is_null()) {
            return std::nullopt;
        }
        return item.at(key).get<int>();
    }

    double degreesToRadians(double degrees) {
        constexpr double pi = 3.14159265358979323846;
        return degrees * pi / 180.0;
    }

    double haversineDistanceMeters(double lat1, double lon1, double lat2, double lon2) {
        constexpr double earthRadiusMeters = 6371000.0;

        const double lat1Rad = degreesToRadians(lat1);
        const double lat2Rad = degreesToRadians(lat2);
        const double deltaLat = degreesToRadians(lat2 - lat1);
        const double deltaLon = degreesToRadians(lon2 - lon1);

        const double sinHalfLat = std::sin(deltaLat / 2.0);
        const double sinHalfLon = std::sin(deltaLon / 2.0);

        const double a =
            sinHalfLat * sinHalfLat +
            std::cos(lat1Rad) * std::cos(lat2Rad) * sinHalfLon * sinHalfLon;

        const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
        return earthRadiusMeters * c;
    }
}

TransitGraph TransitGraph::loadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Nie można otworzyć pliku JSON: " + path);
    }

    json document;
    file >> document;

    TransitGraph graph;

    if (!document.contains("stops") || !document.contains("routes") ||
        !document.contains("trips") || !document.contains("connections")) {
        throw std::runtime_error(
            "JSON nie ma oczekiwanej struktury. Wymagane pola: stops, routes, trips, connections."
        );
    }

    for (const auto& item : document.at("stops")) {
        Stop stop;
        stop.index = requiredValue<int>(item, "index");
        stop.id = requiredValue<std::string>(item, "id");
        stop.code = item.value("code", "");
        stop.name = requiredValue<std::string>(item, "name");
        stop.lat = item.value("lat", 0.0);
        stop.lon = item.value("lon", 0.0);
        stop.platformCode = item.value("platform_code", "");
        stop.wheelchairBoarding = optionalInt(item, "wheelchair_boarding");
        graph.stops.push_back(std::move(stop));
    }

    for (const auto& item : document.at("routes")) {
        Route route;
        route.index = requiredValue<int>(item, "index");
        route.id = requiredValue<std::string>(item, "id");
        route.agencyId = item.value("agency_id", "");
        route.shortName = item.value("short_name", "");
        route.longName = item.value("long_name", "");
        route.description = item.value("description", "");
        route.type = item.value("type", -1);
        graph.routes.push_back(std::move(route));
    }

    for (const auto& item : document.at("trips")) {
        Trip trip;
        trip.index = requiredValue<int>(item, "index");
        trip.id = requiredValue<std::string>(item, "id");
        trip.routeIndex = requiredValue<int>(item, "route_index");
        trip.routeId = item.value("route_id", "");
        trip.serviceId = item.value("service_id", "");
        trip.headsign = item.value("headsign", "");
        trip.directionId = optionalInt(item, "direction_id");
        trip.shapeId = item.value("shape_id", "");
        graph.trips.push_back(std::move(trip));
    }

    for (const auto& item : document.at("connections")) {
        Connection connection;
        connection.from = requiredValue<int>(item, "from");
        connection.to = requiredValue<int>(item, "to");
        connection.route = requiredValue<int>(item, "route");
        connection.trip = requiredValue<int>(item, "trip");
        connection.departure = requiredValue<int>(item, "departure");
        connection.arrival = requiredValue<int>(item, "arrival");
        connection.travelTime = item.value("travel_time", connection.arrival - connection.departure);
        connection.fromSequence = item.value("from_sequence", -1);
        connection.toSequence = item.value("to_sequence", -1);
        graph.connections.push_back(connection);
    }

    graph.buildAdjacency();
    return graph;
}

void TransitGraph::buildAdjacency() {
    adjacency.assign(stops.size(), {});

    for (int connectionIndex = 0; connectionIndex < static_cast<int>(connections.size()); ++connectionIndex) {
        const Connection& connection = connections[connectionIndex];

        if (connection.from < 0 || connection.from >= static_cast<int>(stops.size())) {
            throw std::runtime_error("Connection ma niepoprawny indeks from: " + std::to_string(connection.from));
        }
        if (connection.to < 0 || connection.to >= static_cast<int>(stops.size())) {
            throw std::runtime_error("Connection ma niepoprawny indeks to: " + std::to_string(connection.to));
        }

        adjacency[connection.from].push_back(connectionIndex);
    }

    for (auto& outgoing : adjacency) {
        std::sort(outgoing.begin(), outgoing.end(), [this](int left, int right) {
            const Connection& a = connections[left];
            const Connection& b = connections[right];

            if (a.isWalking() != b.isWalking()) {
                return a.isWalking() && !b.isWalking();
            }
            if (a.departure != b.departure) {
                return a.departure < b.departure;
            }
            if (a.arrival != b.arrival) {
                return a.arrival < b.arrival;
            }
            return a.to < b.to;
        });
    }
}

int TransitGraph::addWalkingConnections(
    double maxWalkingDistanceMeters,
    double walkingSpeedMetersPerSecond,
    int walkingPenaltySeconds
) {
    if (maxWalkingDistanceMeters <= 0.0) {
        return 0;
    }
    if (walkingSpeedMetersPerSecond <= 0.0) {
        throw std::runtime_error("Prędkość pieszego musi być większa od zera.");
    }

    int addedConnections = 0;
    const int stopCount = static_cast<int>(stops.size());

    for (int i = 0; i < stopCount; ++i) {
        for (int j = i + 1; j < stopCount; ++j) {
            const double distance = haversineDistanceMeters(
                stops[i].lat,
                stops[i].lon,
                stops[j].lat,
                stops[j].lon
            );

            if (distance > maxWalkingDistanceMeters) {
                continue;
            }

            const int walkingTime =
                static_cast<int>(std::ceil(distance / walkingSpeedMetersPerSecond)) + walkingPenaltySeconds;

            Connection forward;
            forward.from = i;
            forward.to = j;
            forward.route = -1;
            forward.trip = -1;
            forward.departure = -1;
            forward.arrival = -1;
            forward.travelTime = std::max(1, walkingTime);
            forward.fromSequence = -1;
            forward.toSequence = -1;
            forward.type = ConnectionType::Walk;
            forward.distanceMeters = distance;
            connections.push_back(forward);
            ++addedConnections;

            Connection backward = forward;
            backward.from = j;
            backward.to = i;
            connections.push_back(backward);
            ++addedConnections;
        }
    }

    buildAdjacency();
    return addedConnections;
}

const std::vector<Stop>& TransitGraph::getStops() const {
    return stops;
}

const std::vector<Route>& TransitGraph::getRoutes() const {
    return routes;
}

const std::vector<Trip>& TransitGraph::getTrips() const {
    return trips;
}

const std::vector<Connection>& TransitGraph::getConnections() const {
    return connections;
}

const std::vector<std::vector<int>>& TransitGraph::getAdjacency() const {
    return adjacency;
}

const std::vector<int>& TransitGraph::outgoingConnections(int stopIndex) const {
    if (stopIndex < 0 || stopIndex >= static_cast<int>(adjacency.size())) {
        throw std::out_of_range("Niepoprawny indeks przystanku: " + std::to_string(stopIndex));
    }
    return adjacency[stopIndex];
}

std::vector<int> TransitGraph::findStopsByName(const std::string& text) const {
    const std::string needle = normalizeText(text);
    std::vector<int> results;

    for (const Stop& stop : stops) {
        const std::string haystack = normalizeText(stop.name + " " + stop.code + " " + stop.id);
        if (haystack.find(needle) != std::string::npos) {
            results.push_back(stop.index);
        }
    }

    return results;
}

std::vector<int> TransitGraph::findStopsByExactName(const std::string& name) const {
    std::vector<int> result;

    const std::string normalizedQuery = normalizeText(name);

    for (size_t i = 0; i < stops.size(); ++i) {
        if (normalizeText(stops[i].name) == normalizedQuery) {
            result.push_back(static_cast<int>(i));
        }
    }

    return result;
}

void TransitGraph::printStats() const {
    int stopsWithOutgoing = 0;
    int maxOutgoing = 0;
    int totalOutgoing = 0;
    int transitConnections = 0;
    int walkingConnections = 0;

    for (const Connection& connection : connections) {
        if (connection.isWalking()) {
            ++walkingConnections;
        } else {
            ++transitConnections;
        }
    }

    for (const auto& outgoing : adjacency) {
        if (!outgoing.empty()) {
            ++stopsWithOutgoing;
        }
        maxOutgoing = std::max(maxOutgoing, static_cast<int>(outgoing.size()));
        totalOutgoing += static_cast<int>(outgoing.size());
    }

    std::cout << "=== TransitGraph: statystyki ===\n";
    std::cout << "Przystanki:              " << stops.size() << '\n';
    std::cout << "Trasy / linie:           " << routes.size() << '\n';
    std::cout << "Kursy / trips:           " << trips.size() << '\n';
    std::cout << "Krawędzie / connections: " << connections.size() << '\n';
    std::cout << "  - komunikacja:         " << transitConnections << '\n';
    std::cout << "  - piesze:              " << walkingConnections << '\n';
    std::cout << "Przystanki z wyjściami:  " << stopsWithOutgoing << '\n';
    std::cout << "Śr. liczba wyjść:        "
              << (stops.empty() ? 0.0 : static_cast<double>(totalOutgoing) / static_cast<double>(stops.size()))
              << '\n';
    std::cout << "Maks. liczba wyjść:      " << maxOutgoing << "\n\n";
}

void TransitGraph::printStopsByName(const std::string& text, int maxResults) const {
    const auto results = findStopsByName(text);

    std::cout << "=== Wyniki wyszukiwania przystanku: \"" << text << "\" ===\n";
    if (results.empty()) {
        std::cout << "Brak wyników.\n\n";
        return;
    }

    const int limit = std::min(maxResults, static_cast<int>(results.size()));
    for (int i = 0; i < limit; ++i) {
        const Stop& stop = stopAt(results[i]);
        std::cout << '[' << stop.index << "] " << stop.name
                  << " | id=" << stop.id
                  << " | code=" << stop.code
                  << " | out=" << adjacency[stop.index].size()
                  << " | lat=" << stop.lat
                  << " | lon=" << stop.lon
                  << '\n';
    }

    if (static_cast<int>(results.size()) > limit) {
        std::cout << "... oraz " << (results.size() - limit) << " kolejnych wyników.\n";
    }
    std::cout << '\n';
}

void TransitGraph::printGraphSample(int maxStops, int maxEdgesPerStop) const {
    std::cout << "=== Próbka grafu ===\n";
    std::cout << "Format: [from] nazwa -> [to] nazwa | linia | odjazd -> przyjazd | trip\n\n";

    int printedStops = 0;
    for (const Stop& fromStop : stops) {
        const auto& outgoing = adjacency[fromStop.index];
        if (outgoing.empty()) {
            continue;
        }

        if (printedStops >= maxStops) {
            break;
        }

        std::cout << '[' << fromStop.index << "] " << fromStop.name
                  << " (" << fromStop.id << ")"
                  << " | outgoing=" << outgoing.size() << '\n';

        const int edgeLimit = std::min(maxEdgesPerStop, static_cast<int>(outgoing.size()));
        for (int i = 0; i < edgeLimit; ++i) {
            const Connection& connection = connections[outgoing[i]];
            const Stop& toStop = stopAt(connection.to);

            std::cout << "  -> [" << toStop.index << "] " << toStop.name;
            if (connection.isWalking()) {
                std::cout << " | pieszo"
                          << " | dystans " << std::fixed << std::setprecision(1) << connection.distanceMeters << " m"
                          << " | czas " << durationToText(connection.travelTime)
                          << '\n';
            } else {
                const Route& route = routeAt(connection.route);
                const Trip& trip = tripAt(connection.trip);
                std::cout << " | linia " << routeLabel(route)
                          << " | " << secondsToTime(connection.departure)
                          << " -> " << secondsToTime(connection.arrival)
                          << " | " << (connection.travelTime / 60) << " min"
                          << " | trip=" << trip.id
                          << '\n';
            }
        }

        if (static_cast<int>(outgoing.size()) > edgeLimit) {
            std::cout << "  ... oraz " << (outgoing.size() - edgeLimit) << " kolejnych krawędzi\n";
        }

        std::cout << '\n';
        ++printedStops;
    }

    if (printedStops == 0) {
        std::cout << "Graf nie zawiera przystanków z krawędziami wychodzącymi.\n\n";
    }
}

void TransitGraph::printStopNeighborhood(int stopIndex, int maxEdges) const {
    const Stop& stop = stopAt(stopIndex);
    const auto& outgoing = outgoingConnections(stopIndex);

    std::cout << "=== Sąsiedztwo przystanku ===\n";
    std::cout << '[' << stop.index << "] " << stop.name
              << " | id=" << stop.id
              << " | code=" << stop.code
              << " | outgoing=" << outgoing.size()
              << "\n\n";

    const int limit = std::min(maxEdges, static_cast<int>(outgoing.size()));
    for (int i = 0; i < limit; ++i) {
        const Connection& connection = connections[outgoing[i]];
        const Stop& toStop = stopAt(connection.to);

        std::cout << std::setw(4) << i + 1 << ". ";
        if (connection.isWalking()) {
            std::cout << "pieszo"
                      << " | czas " << durationToText(connection.travelTime)
                      << " | dystans " << std::fixed << std::setprecision(1) << connection.distanceMeters << " m"
                      << " | do [" << toStop.index << "] " << toStop.name
                      << '\n';
        } else {
            const Route& route = routeAt(connection.route);
            const Trip& trip = tripAt(connection.trip);
            std::cout << secondsToTime(connection.departure)
                      << " -> " << secondsToTime(connection.arrival)
                      << " | linia " << routeLabel(route)
                      << " | do [" << toStop.index << "] " << toStop.name
                      << " | trip=" << trip.id
                      << " | headsign=" << trip.headsign
                      << '\n';
        }
    }

    if (static_cast<int>(outgoing.size()) > limit) {
        std::cout << "... oraz " << (outgoing.size() - limit) << " kolejnych połączeń.\n";
    }
    std::cout << '\n';
}

void TransitGraph::printSearchResult(const SearchResult& result, bool printPath) const {
    std::cout << "=== Wynik: " << result.methodName << " ===\n";
    std::cout << "Start:      [" << result.startStop << "] " << stopAt(result.startStop).name << '\n';
    std::cout << "Cel:        [" << result.targetStop << "] " << stopAt(result.targetStop).name << '\n';
    std::cout << "Godzina:    " << secondsToTime(result.requestedStartTime) << '\n';

    if (!result.found) {
        std::cout << "Status:     nie znaleziono połączenia\n";
        if (result.stoppedByLimit) {
            std::cout << "Uwaga:      DFS został przerwany przez limit odwiedzonych stanów.\n";
        }
        std::cout << "Stany:      " << result.visitedStates << '\n';
        std::cout << "Krawędzie:  " << result.consideredEdges << '\n';
        std::cout << "Czas CPU:   " << std::fixed << std::setprecision(3) << result.elapsedMilliseconds << " ms\n\n";
        return;
    }

    std::cout << "Status:     znaleziono połączenie\n";
    std::cout << "Przyjazd:   " << secondsToTime(result.arrivalTime) << '\n';
    std::cout << "Czas trasy: " << durationToText(result.totalTravelTime) << '\n';
    std::cout << "Przesiadki: " << result.transfers << '\n';
    std::cout << "Krawędzie w ścieżce: " << result.pathConnections.size() << '\n';
    std::cout << "Odwiedzone stany:    " << result.visitedStates << '\n';
    std::cout << "Rozważone krawędzie: " << result.consideredEdges << '\n';
    std::cout << "Czas obliczeń:       " << std::fixed << std::setprecision(3) << result.elapsedMilliseconds << " ms\n";

    if (result.stoppedByLimit) {
        std::cout << "Uwaga: DFS osiągnął limit odwiedzonych stanów, więc wynik może być najlepszym znalezionym w limicie, a nie globalnym optimum.\n";
    }

    if (!printPath) {
        std::cout << '\n';
        return;
    }

    std::cout << "\nŚcieżka:\n";
    if (result.pathConnections.empty()) {
        std::cout << "  Start i cel to ten sam przystanek.\n\n";
        return;
    }

    int currentTime = result.requestedStartTime;
    int previousTransitTrip = -1;
    
    for (int i = 0; i < static_cast<int>(result.pathConnections.size()); ++i) {
        const int connectionIndex = result.pathConnections[i];
        const Connection& connection = connections[connectionIndex];
        const Stop& fromStop = stopAt(connection.from);
        const Stop& toStop = stopAt(connection.to);

        std::cout << std::setw(3) << i + 1 << ". "
                  << "[" << fromStop.index << "] " << fromStop.name
                  << " -> [" << toStop.index << "] " << toStop.name;

        if (connection.isWalking()) {
            std::cout << " | pieszo"
                      << " | czas " << durationToText(connection.travelTime)
                      << " | dystans " << std::fixed << std::setprecision(1) << connection.distanceMeters << " m"
                      << '\n';
            currentTime += connection.travelTime;
            continue;
        }

        const Route& route = routeAt(connection.route);
        const Trip& trip = tripAt(connection.trip);

        const int waitTime = std::max(0, connection.departure - currentTime);
        const int rideTime = std::max(0, connection.arrival - connection.departure);

        std::cout << " | linia " << routeLabel(route)
                  << " | " << secondsToTime(connection.departure)
                  << "-" << secondsToTime(connection.arrival)
                  << " | jazda " << durationToText(rideTime);

        if (waitTime > 0) {
            std::cout << " | czekanie " << durationToText(waitTime);
        }

        if (previousTransitTrip != -1 && previousTransitTrip != connection.trip) {
            std::cout << " | PRZESIADKA";
        }
        
        std::cout << " | trip=" << trip.id << '\n';
        previousTransitTrip = connection.trip;
        currentTime = connection.arrival;
    }

    std::cout << '\n';
}

void TransitGraph::compareAlgorithms(
    int startStop,
    int targetStop,
    int startTimeSeconds,
    int dfsMaxDepth,
    int dfsMaxDurationSeconds,
    int dfsMaxVisitedStates
) const {
    const SearchResult dfsResult = findEarliestArrivalDfs(
        startStop,
        targetStop,
        startTimeSeconds,
        dfsMaxDepth,
        dfsMaxDurationSeconds,
        dfsMaxVisitedStates
    );

    const SearchResult dijkstraResult = findEarliestArrivalDijkstra(startStop, targetStop, startTimeSeconds);

    std::cout << "=== Porównanie metod ===\n";
    std::cout << "Start: [" << startStop << "] " << stopAt(startStop).name
              << " -> Cel: [" << targetStop << "] " << stopAt(targetStop).name
              << " | godzina startu: " << secondsToTime(startTimeSeconds) << "\n\n";

    std::cout << std::left
              << std::setw(20) << "Metoda"
              << std::setw(14) << "Znaleziono"
              << std::setw(12) << "Przyjazd"
              << std::setw(14) << "Czas trasy"
              << std::setw(12) << "Przesiadki"
              << std::setw(14) << "Stany"
              << std::setw(14) << "Krawędzie"
              << std::setw(14) << "CPU [ms]"
              << "Uwagi"
              << '\n';

    const auto printRow = [this](const SearchResult& result) {
        std::cout << std::left
                  << std::setw(20) << result.methodName
                  << std::setw(14) << (result.found ? "tak" : "nie")
                  << std::setw(12) << (result.found ? secondsToTime(result.arrivalTime) : "-")
                  << std::setw(14) << (result.found ? durationToText(result.totalTravelTime) : "-")
                  << std::setw(12) << (result.found ? std::to_string(result.transfers) : "-")
                  << std::setw(14) << result.visitedStates
                  << std::setw(14) << result.consideredEdges
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.elapsedMilliseconds
                  << (result.stoppedByLimit ? "limit DFS" : "")
                  << '\n';
    };

    printRow(dfsResult);
    printRow(dijkstraResult);
    std::cout << "\n";

    printSearchResult(dfsResult, true);
    printSearchResult(dijkstraResult, true);
}

void TransitGraph::compareAlgorithmsByNames(
    const std::string& startStopName,
    const std::string& targetStopName,
    int startTimeSeconds,
    int dfsMaxDepth,
    int dfsMaxDurationSeconds,
    int dfsMaxVisitedStates
) const {
    const std::vector<int> startCandidates = findStopsByExactName(startStopName);
    const std::vector<int> targetCandidates = findStopsByExactName(targetStopName);

    if (startCandidates.empty()) {
        throw std::runtime_error(
            "Nie znaleziono przystanków o dokładnej nazwie startowej: \"" + startStopName + "\". "
            "Użyj --find, żeby sprawdzić dostępne nazwy."
        );
    }

    if (targetCandidates.empty()) {
        throw std::runtime_error(
            "Nie znaleziono przystanków o dokładnej nazwie docelowej: \"" + targetStopName + "\". "
            "Użyj --find, żeby sprawdzić dostępne nazwy."
        );
    }

    const SearchResult dfsResult = findEarliestArrivalDfs(
        startCandidates,
        targetCandidates,
        startTimeSeconds,
        dfsMaxDepth,
        dfsMaxDurationSeconds,
        dfsMaxVisitedStates
    );

    const SearchResult dijkstraResult = findEarliestArrivalDijkstra(
        startCandidates,
        targetCandidates,
        startTimeSeconds
    );

    const auto printCandidates = [this](const std::string& label, const std::vector<int>& candidates) {
        std::cout << label << " kandydaci: " << candidates.size() << '\n';
        const int limit = std::min(12, static_cast<int>(candidates.size()));
        for (int i = 0; i < limit; ++i) {
            const Stop& stop = stopAt(candidates[i]);
            std::cout << "  [" << stop.index << "] " << stop.name
                      << " | id=" << stop.id
                      << " | code=" << stop.code
                      << " | out=" << adjacency[stop.index].size()
                      << "\n";
        }
        if (static_cast<int>(candidates.size()) > limit) {
            std::cout << "  ... oraz " << (candidates.size() - limit) << " kolejnych\n";
        }
    };

    std::cout << "=== Porównanie metod po dokładnych nazwach przystanków ===\n";
    std::cout << "Start: \"" << startStopName << "\" -> Cel: \"" << targetStopName << "\""
              << " | godzina startu: " << secondsToTime(startTimeSeconds) << "\n\n";

    printCandidates("Start", startCandidates);
    printCandidates("Cel", targetCandidates);
    std::cout << '\n';

    std::cout << std::left
              << std::setw(20) << "Metoda"
              << std::setw(14) << "Znaleziono"
              << std::setw(12) << "Przyjazd"
              << std::setw(14) << "Czas trasy"
              << std::setw(12) << "Przesiadki"
              << std::setw(14) << "Stany"
              << std::setw(14) << "Krawędzie"
              << std::setw(14) << "CPU [ms]"
              << "Uwagi"
              << '\n';

    const auto printRow = [this](const SearchResult& result) {
        std::cout << std::left
                  << std::setw(20) << result.methodName
                  << std::setw(14) << (result.found ? "tak" : "nie")
                  << std::setw(12) << (result.found ? secondsToTime(result.arrivalTime) : "-")
                  << std::setw(14) << (result.found ? durationToText(result.totalTravelTime) : "-")
                  << std::setw(12) << (result.found ? std::to_string(result.transfers) : "-")
                  << std::setw(14) << result.visitedStates
                  << std::setw(14) << result.consideredEdges
                  << std::setw(14) << std::fixed << std::setprecision(3) << result.elapsedMilliseconds
                  << (result.stoppedByLimit ? "limit DFS" : "")
                  << '\n';
    };

    printRow(dfsResult);
    printRow(dijkstraResult);
    std::cout << "\n";

    printSearchResult(dfsResult, true);
    printSearchResult(dijkstraResult, true);
}

void TransitGraph::exportDot(const std::string& path, int maxStops, int maxEdgesPerStop) const {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Nie można zapisać pliku DOT: " + path);
    }

    std::unordered_set<int> includedStops;
    std::vector<int> selectedStops;

    for (const Stop& stop : stops) {
        if (!adjacency[stop.index].empty()) {
            selectedStops.push_back(stop.index);
            includedStops.insert(stop.index);
            if (static_cast<int>(selectedStops.size()) >= maxStops) {
                break;
            }
        }
    }

    // Dodaj też przystanki docelowe, żeby krawędzie nie wskazywały na niewidoczne węzły.
    for (int fromIndex : selectedStops) {
        const auto& outgoing = adjacency[fromIndex];
        const int edgeLimit = std::min(maxEdgesPerStop, static_cast<int>(outgoing.size()));
        for (int i = 0; i < edgeLimit; ++i) {
            includedStops.insert(connections[outgoing[i]].to);
        }
    }

    file << "digraph TransitGraph {\n";
    file << "  graph [rankdir=LR, overlap=false, splines=true];\n";
    file << "  node [shape=box, style=rounded, fontsize=10];\n";
    file << "  edge [fontsize=9];\n\n";

    for (int stopIndex : includedStops) {
        const Stop& stop = stopAt(stopIndex);
        file << "  s" << stop.index
             << " [label=\"[" << stop.index << "] " << escapeDot(stop.name)
             << "\\n" << escapeDot(stop.id) << "\"];\n";
    }

    file << '\n';

    int edgeCount = 0;
    for (int fromIndex : selectedStops) {
        const auto& outgoing = adjacency[fromIndex];
        const int edgeLimit = std::min(maxEdgesPerStop, static_cast<int>(outgoing.size()));

        for (int i = 0; i < edgeLimit; ++i) {
            const Connection& connection = connections[outgoing[i]];

            file << "  s" << connection.from
                 << " -> s" << connection.to
                 << " [label=\"";

            if (connection.isWalking()) {
                file << "pieszo " << static_cast<int>(std::round(connection.distanceMeters)) << "m / "
                     << durationToText(connection.travelTime);
            } else {
                const Route& route = routeAt(connection.route);
                file << escapeDot(routeLabel(route))
                     << " " << secondsToTime(connection.departure)
                     << "-" << secondsToTime(connection.arrival);
            }

            file << "\"];\n";
            ++edgeCount;
        }
    }

    file << "}\n";

    std::cout << "Zapisano plik DOT: " << path
              << " | węzły=" << includedStops.size()
              << " | krawędzie=" << edgeCount
              << "\n";
}

std::string TransitGraph::secondsToTime(int seconds) {
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int sec = seconds % 60;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hours
           << ':' << std::setw(2) << minutes
           << ':' << std::setw(2) << sec;
    return output.str();
}

std::string TransitGraph::durationToText(int seconds) {
    if (seconds < 0) {
        return "-";
    }

    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int sec = seconds % 60;

    std::ostringstream output;
    if (hours > 0) {
        output << hours << "h ";
    }
    output << minutes << "min";
    if (hours == 0 && sec > 0) {
        output << " " << sec << "s";
    }
    return output.str();
}

std::string TransitGraph::toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string TransitGraph::escapeDot(std::string value) {
    std::string result;
    result.reserve(value.size());

    for (char character : value) {
        if (character == '"') {
            result += "\\\"";
        } else if (character == '\\') {
            result += "\\\\";
        } else {
            result += character;
        }
    }

    return result;
}

std::string TransitGraph::routeLabel(const Route& route) {
    if (!route.shortName.empty()) {
        return route.shortName;
    }
    if (!route.longName.empty()) {
        return route.longName;
    }
    return route.id;
}

int TransitGraph::countTransfers(const std::vector<int>& pathConnections, const std::vector<Connection>& connections) {
    int transfers = 0;
    int previousTransitTrip = -1;

    for (int connectionIndex : pathConnections) {
        const Connection& connection = connections[connectionIndex];
        if (connection.isWalking()) {
            continue;
        }

        if (previousTransitTrip != -1 && connection.trip != previousTransitTrip) {
            ++transfers;
        }
        previousTransitTrip = connection.trip;
    }

    return transfers;
}

const Stop& TransitGraph::stopAt(int index) const {
    if (index < 0 || index >= static_cast<int>(stops.size())) {
        throw std::out_of_range("Niepoprawny indeks przystanku: " + std::to_string(index));
    }
    return stops[index];
}

const Route& TransitGraph::routeAt(int index) const {
    if (index < 0 || index >= static_cast<int>(routes.size())) {
        throw std::out_of_range("Niepoprawny indeks trasy: " + std::to_string(index));
    }
    return routes[index];
}

const Trip& TransitGraph::tripAt(int index) const {
    if (index < 0 || index >= static_cast<int>(trips.size())) {
        throw std::out_of_range("Niepoprawny indeks kursu: " + std::to_string(index));
    }
    return trips[index];
}

void TransitGraph::validateStopIndex(int stopIndex, const std::string& label) const {
    if (stopIndex < 0 || stopIndex >= static_cast<int>(stops.size())) {
        throw std::out_of_range(label + " ma niepoprawny indeks przystanku: " + std::to_string(stopIndex));
    }
}
