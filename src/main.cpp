#include "TransitGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
    struct ProgramOptions {
        std::string jsonPath;
        bool printStats = true;
        bool printGraph = false;
        int limitStops = 10;
        int limitEdges = 8;
        std::string findStopText;
        int neighborhoodStopIndex = -1;
        std::string dotPath;
        int dotStops = 50;
        int dotEdges = 4;

        bool runCompare = false;
        int compareStartStop = -1;
        int compareTargetStop = -1;
        int compareStartTime = 0;

        bool runCompareByNames = false;
        std::string compareStartName;
        std::string compareTargetName;
        int dfsMaxDepth = 8;
        int dfsMaxDurationSeconds = 4 * 3600;
        int dfsMaxVisitedStates = 250000;
    };

    void printUsage(const char* executableName) {
        std::cout
            << "Użycie:\n"
            << "  " << executableName << " graph.json [opcje]\n\n"
            << "Opcje podstawowe:\n"
            << "  --stats                         Wypisz statystyki grafu. Domyślnie włączone.\n"
            << "  --print                         Wypisz próbkę grafu w konsoli.\n"
            << "  --limit-stops N                 Limit przystanków przy --print. Domyślnie 10.\n"
            << "  --limit-edges N                 Limit krawędzi na przystanek przy --print. Domyślnie 8.\n"
            << "  --find \"tekst\"                 Znajdź przystanki po fragmencie nazwy / id / kodu.\n"
            << "  --neighborhood INDEX            Wypisz połączenia wychodzące z jednego przystanku.\n"
            << "  --dot output.dot                Eksportuj próbkę grafu do Graphviz DOT.\n"
            << "  --dot-stops N                   Limit przystanków w DOT. Domyślnie 50.\n"
            << "  --dot-edges N                   Limit krawędzi na przystanek w DOT. Domyślnie 4.\n\n"
            << "Porównanie algorytmów:\n"
            << "  --compare START TARGET HH:MM    Porównaj DFS ograniczony i Dijkstrę czasową dla indeksów przystanków.\n"
            << "  --compare-names START CEL HH:MM Porównaj algorytmy dla wszystkich przystanków o dokładnych nazwach.\n"
            << "  --dfs-depth N                   Maksymalna liczba krawędzi w ścieżce DFS. Domyślnie 8.\n"
            << "  --dfs-duration-min N            Maksymalny czas podróży dla DFS w minutach. Domyślnie 240.\n"
            << "  --dfs-states N                  Maksymalna liczba odwiedzonych stanów DFS. Domyślnie 250000.\n"
            << "  --help                          Pokaż pomoc.\n\n"
            << "Przykłady:\n"
            << "  " << executableName << " graph.json --find \"Galeria\"\n"
            << "  " << executableName << " graph.json --compare 123 456 08:00\n"
            << "  " << executableName << " graph.json --compare-names \"Dworzec Główny\" \"Galeria Dominikańska\" 08:00\n"
            << "  " << executableName << " graph.json --compare 123 456 08:00 --dfs-depth 10 --dfs-duration-min 180\n"
            << "  " << executableName << " graph.json --dot sample.dot --dot-stops 30 --dot-edges 3\n";
    }

    int readIntArgument(int& index, int argc, char* argv[], const std::string& optionName) {
        if (index + 1 >= argc) {
            throw std::runtime_error("Opcja " + optionName + " wymaga wartości liczbowej.");
        }
        ++index;
        return std::stoi(argv[index]);
    }

    std::string readStringArgument(int& index, int argc, char* argv[], const std::string& optionName) {
        if (index + 1 >= argc) {
            throw std::runtime_error("Opcja " + optionName + " wymaga wartości tekstowej.");
        }
        ++index;
        return argv[index];
    }

    int parseTimeToSeconds(const std::string& value) {
        int hours = 0;
        int minutes = 0;
        int seconds = 0;

        const std::size_t firstColon = value.find(':');
        if (firstColon == std::string::npos) {
            throw std::runtime_error("Niepoprawny format godziny: " + value + ". Użyj HH:MM albo HH:MM:SS.");
        }

        const std::size_t secondColon = value.find(':', firstColon + 1);
        hours = std::stoi(value.substr(0, firstColon));

        if (secondColon == std::string::npos) {
            minutes = std::stoi(value.substr(firstColon + 1));
        } else {
            minutes = std::stoi(value.substr(firstColon + 1, secondColon - firstColon - 1));
            seconds = std::stoi(value.substr(secondColon + 1));
        }

        if (hours < 0 || minutes < 0 || minutes >= 60 || seconds < 0 || seconds >= 60) {
            throw std::runtime_error("Niepoprawna godzina: " + value);
        }

        return hours * 3600 + minutes * 60 + seconds;
    }

    ProgramOptions parseArguments(int argc, char* argv[]) {
        if (argc < 2) {
            printUsage(argv[0]);
            throw std::runtime_error("Nie podano ścieżki do graph.json.");
        }

        ProgramOptions options;

        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];

            if (argument == "--help" || argument == "-h") {
                printUsage(argv[0]);
                std::exit(0);
            } else if (argument == "--stats") {
                options.printStats = true;
            } else if (argument == "--print") {
                options.printGraph = true;
            } else if (argument == "--limit-stops") {
                options.limitStops = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--limit-edges") {
                options.limitEdges = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--find") {
                options.findStopText = readStringArgument(i, argc, argv, argument);
            } else if (argument == "--neighborhood") {
                options.neighborhoodStopIndex = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--dot") {
                options.dotPath = readStringArgument(i, argc, argv, argument);
            } else if (argument == "--dot-stops") {
                options.dotStops = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--dot-edges") {
                options.dotEdges = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--compare") {
                if (i + 3 >= argc) {
                    throw std::runtime_error("Opcja --compare wymaga argumentów: START TARGET HH:MM.");
                }
                options.runCompare = true;
                options.compareStartStop = std::stoi(argv[++i]);
                options.compareTargetStop = std::stoi(argv[++i]);
                options.compareStartTime = parseTimeToSeconds(argv[++i]);
            } else if (argument == "--compare-names") {
                if (i + 3 >= argc) {
                    throw std::runtime_error("Opcja --compare-names wymaga argumentów: START_NAME TARGET_NAME HH:MM.");
                }
                options.runCompareByNames = true;
                options.compareStartName = argv[++i];
                options.compareTargetName = argv[++i];
                options.compareStartTime = parseTimeToSeconds(argv[++i]);
            } else if (argument == "--dfs-depth") {
                options.dfsMaxDepth = readIntArgument(i, argc, argv, argument);
            } else if (argument == "--dfs-duration-min") {
                options.dfsMaxDurationSeconds = readIntArgument(i, argc, argv, argument) * 60;
            } else if (argument == "--dfs-states") {
                options.dfsMaxVisitedStates = readIntArgument(i, argc, argv, argument);
            } else if (!argument.empty() && argument[0] == '-') {
                throw std::runtime_error("Nieznana opcja: " + argument);
            } else if (options.jsonPath.empty()) {
                options.jsonPath = argument;
            } else {
                throw std::runtime_error("Nieoczekiwany argument: " + argument);
            }
        }

        if (options.jsonPath.empty()) {
            throw std::runtime_error("Nie podano ścieżki do graph.json.");
        }

        return options;
    }
}

int main(int argc, char* argv[]) {
    try {
        const ProgramOptions options = parseArguments(argc, argv);

        TransitGraph graph = TransitGraph::loadFromJson(options.jsonPath);

        if (options.printStats) {
            graph.printStats();
        }

        if (!options.findStopText.empty()) {
            graph.printStopsByName(options.findStopText);
        }

        if (options.neighborhoodStopIndex >= 0) {
            graph.printStopNeighborhood(options.neighborhoodStopIndex, options.limitEdges);
        }

        if (options.printGraph) {
            graph.printGraphSample(options.limitStops, options.limitEdges);
        }

        if (options.runCompare) {
            graph.compareAlgorithms(
                options.compareStartStop,
                options.compareTargetStop,
                options.compareStartTime,
                options.dfsMaxDepth,
                options.dfsMaxDurationSeconds,
                options.dfsMaxVisitedStates
            );
        }

        if (options.runCompareByNames) {
            graph.compareAlgorithmsByNames(
                options.compareStartName,
                options.compareTargetName,
                options.compareStartTime,
                options.dfsMaxDepth,
                options.dfsMaxDurationSeconds,
                options.dfsMaxVisitedStates
            );
        }

        if (!options.dotPath.empty()) {
            graph.exportDot(options.dotPath, options.dotStops, options.dotEdges);
            std::cout << "Renderowanie przykładowe:\n"
                      << "  dot -Tpng " << options.dotPath << " -o graph.png\n";
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Błąd: " << error.what() << '\n';
        return 1;
    }
}
