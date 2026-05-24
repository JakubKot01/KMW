#!/usr/bin/env python3
"""
gtfs_to_json_parser.py

Parser plików GTFS-like do jednego pliku JSON przygotowanego pod C++.

Typowy przepływ:
    pliki .txt GTFS -> ten skrypt Python -> graph.json -> program C++ z algorytmami

Domyślnie skrypt zapisuje:
    - przystanki jako indeksowane wierzchołki grafu,
    - linie/trasy,
    - kursy/przejazdy,
    - krawędzie grafu tworzone z kolejnych rekordów stop_times.txt.

Przykład:
    python gtfs_to_json_parser.py ./gtfs_data -o graph.json --date 2025-12-01

Lżejszy JSON, bez geometrii shapes.txt:
    python gtfs_to_json_parser.py ./gtfs_data -o graph.json --date 2025-12-01

Z geometrią tras, przydatną do mapy:
    python gtfs_to_json_parser.py ./gtfs_data -o graph_with_shapes.json --date 2025-12-01 --include-shapes

Skompresowany JSON:
    python gtfs_to_json_parser.py ./gtfs_data -o graph.json.gz --date 2025-12-01
"""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import os
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass
from datetime import date, datetime
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple


# -----------------------------------------------------------------------------
# Modele domenowe
# -----------------------------------------------------------------------------

@dataclass(slots=True)
class ServiceCalendar:
    service_id: str
    monday: bool
    tuesday: bool
    wednesday: bool
    thursday: bool
    friday: bool
    saturday: bool
    sunday: bool
    start_date: str
    end_date: str

    def is_active_on(self, selected_date: date) -> bool:
        start = parse_gtfs_date(self.start_date)
        end = parse_gtfs_date(self.end_date)

        if selected_date < start or selected_date > end:
            return False

        weekday = selected_date.weekday()  # Monday = 0
        flags = [
            self.monday,
            self.tuesday,
            self.wednesday,
            self.thursday,
            self.friday,
            self.saturday,
            self.sunday,
        ]
        return flags[weekday]

    def to_json_dict(self) -> Dict[str, Any]:
        return {
            "service_id": self.service_id,
            "monday": self.monday,
            "tuesday": self.tuesday,
            "wednesday": self.wednesday,
            "thursday": self.thursday,
            "friday": self.friday,
            "saturday": self.saturday,
            "sunday": self.sunday,
            "start_date": self.start_date,
            "end_date": self.end_date,
        }


@dataclass(slots=True)
class Route:
    index: int
    id: str
    agency_id: str
    short_name: str
    long_name: str
    description: str
    type: int


@dataclass(slots=True)
class Stop:
    index: int
    id: str
    code: str
    name: str
    lat: float
    lon: float
    platform_code: str
    wheelchair_boarding: Optional[int]


@dataclass(slots=True)
class Trip:
    index: int
    id: str
    route_index: int
    route_id: str
    service_id: str
    headsign: str
    direction_id: Optional[int]
    shape_id: str


@dataclass(slots=True)
class ShapePoint:
    lat: float
    lon: float
    sequence: int
    distance: Optional[float]


@dataclass(slots=True)
class Shape:
    id: str
    points: List[ShapePoint]


@dataclass(slots=True)
class StopEvent:
    stop_index: int
    stop_id: str
    arrival_seconds: int
    departure_seconds: int
    stop_sequence: int
    pickup_type: Optional[int]
    drop_off_type: Optional[int]


@dataclass(slots=True)
class Connection:
    """
    Krawędź grafu czasowego.

    from_stop -> to_stop oznacza możliwość przejazdu między dwoma kolejnymi
    przystankami w ramach konkretnego kursu.
    """

    from_stop: int
    to_stop: int
    route: int
    trip: int
    departure: int
    arrival: int
    travel_time: int
    from_sequence: int
    to_sequence: int


# -----------------------------------------------------------------------------
# Funkcje pomocnicze
# -----------------------------------------------------------------------------

def read_csv_dicts(path: Path) -> Iterable[Dict[str, str]]:
    """Czyta plik CSV/GTFS jako słowniki. Obsługuje BOM przez utf-8-sig."""
    with path.open("r", encoding="utf-8-sig", newline="") as file:
        reader = csv.DictReader(file)
        for row in reader:
            yield {key: (value or "") for key, value in row.items()}


def parse_bool_flag(value: str) -> bool:
    return value.strip() == "1"


def parse_optional_int(value: str) -> Optional[int]:
    value = value.strip()
    if value == "":
        return None
    return int(value)


def parse_optional_float(value: str) -> Optional[float]:
    value = value.strip()
    if value == "":
        return None
    return float(value)


def parse_int(value: str, default: int = 0) -> int:
    value = value.strip()
    if value == "":
        return default
    return int(value)


def parse_float(value: str, default: float = 0.0) -> float:
    value = value.strip()
    if value == "":
        return default
    return float(value)


def parse_gtfs_date(value: str) -> date:
    return datetime.strptime(value, "%Y%m%d").date()


def parse_iso_date(value: str) -> date:
    return datetime.strptime(value, "%Y-%m-%d").date()


def parse_time_to_seconds(value: str) -> int:
    """
    Zamienia czas GTFS na sekundy od początku dnia.

    GTFS dopuszcza godziny większe niż 23, np. 24:11:00, więc nie można używać
    datetime.time. Taki czas oznacza kurs po północy, ale nadal w ramach dnia
    rozkładowego.
    """
    value = value.strip()
    if value == "":
        return 0

    parts = value.split(":")
    if len(parts) != 3:
        raise ValueError(f"Niepoprawny format czasu GTFS: {value!r}")

    hours = int(parts[0])
    minutes = int(parts[1])
    seconds = int(parts[2])
    return hours * 3600 + minutes * 60 + seconds


def seconds_to_time(value: int) -> str:
    hours = value // 3600
    minutes = (value % 3600) // 60
    seconds = value % 60
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


def file_stats(path: Path) -> Dict[str, Any]:
    if not path.exists():
        return {"exists": False}

    stat = path.stat()
    return {
        "exists": True,
        "size_bytes": stat.st_size,
        "modified_timestamp": stat.st_mtime,
    }


def open_output(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "wt", encoding="utf-8")
    return path.open("w", encoding="utf-8")


# -----------------------------------------------------------------------------
# Parser GTFS -> JSON
# -----------------------------------------------------------------------------

class GtfsJsonParser:
    def __init__(
        self,
        gtfs_dir: Path,
        selected_date: Optional[date] = None,
        include_shapes: bool = False,
        include_adjacency: bool = False,
        max_trips: Optional[int] = None,
    ) -> None:
        self.gtfs_dir = gtfs_dir
        self.selected_date = selected_date
        self.include_shapes = include_shapes
        self.include_adjacency = include_adjacency
        self.max_trips = max_trips

        self.services: Dict[str, ServiceCalendar] = {}
        self.calendar_exceptions: Dict[str, Dict[str, int]] = defaultdict(dict)

        self.routes: List[Route] = []
        self.stops: List[Stop] = []
        self.trips: List[Trip] = []
        self.connections: List[Connection] = []
        self.shapes: List[Shape] = []

        self.route_id_to_index: Dict[str, int] = {}
        self.stop_id_to_index: Dict[str, int] = {}
        self.trip_id_to_index: Dict[str, int] = {}

        self.warnings: List[str] = []
        self.counters: Dict[str, int] = defaultdict(int)

    def parse(self) -> Dict[str, Any]:
        self._validate_required_files()
        self._load_calendar()
        self._load_routes()
        self._load_stops()
        self._load_trips()
        self._load_connections_from_stop_times()

        if self.include_shapes:
            self._load_shapes()

        return self._build_json_document()

    def export(self, output_path: Path, pretty: bool = False) -> None:
        document = self.parse()
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with open_output(output_path) as file:
            json.dump(
                document,
                file,
                ensure_ascii=False,
                indent=2 if pretty else None,
                separators=None if pretty else (",", ":"),
            )
            file.write("\n")

    def _path(self, filename: str) -> Path:
        return self.gtfs_dir / filename

    def _validate_required_files(self) -> None:
        required_files = [
            "calendar.txt",
            "routes.txt",
            "stops.txt",
            "trips.txt",
            "stop_times.txt",
        ]
        missing = [name for name in required_files if not self._path(name).exists()]
        if missing:
            joined = ", ".join(missing)
            raise FileNotFoundError(f"Brakuje wymaganych plików GTFS: {joined}")

    def _load_calendar(self) -> None:
        calendar_path = self._path("calendar.txt")
        for row in read_csv_dicts(calendar_path):
            service = ServiceCalendar(
                service_id=row["service_id"],
                monday=parse_bool_flag(row["monday"]),
                tuesday=parse_bool_flag(row["tuesday"]),
                wednesday=parse_bool_flag(row["wednesday"]),
                thursday=parse_bool_flag(row["thursday"]),
                friday=parse_bool_flag(row["friday"]),
                saturday=parse_bool_flag(row["saturday"]),
                sunday=parse_bool_flag(row["sunday"]),
                start_date=row["start_date"],
                end_date=row["end_date"],
            )
            self.services[service.service_id] = service

        self.counters["services"] = len(self.services)

        # Opcjonalny standardowy plik GTFS. W aktualnym zestawie może go nie być,
        # ale obsługa nic nie kosztuje.
        calendar_dates_path = self._path("calendar_dates.txt")
        if calendar_dates_path.exists():
            for row in read_csv_dicts(calendar_dates_path):
                service_id = row["service_id"]
                service_date = row["date"]
                exception_type = parse_int(row["exception_type"])
                self.calendar_exceptions[service_id][service_date] = exception_type

    def _is_service_active(self, service_id: str) -> bool:
        if self.selected_date is None:
            return True

        service_date_key = self.selected_date.strftime("%Y%m%d")
        exception = self.calendar_exceptions.get(service_id, {}).get(service_date_key)
        if exception == 1:
            return True
        if exception == 2:
            return False

        service = self.services.get(service_id)
        if service is None:
            return False

        return service.is_active_on(self.selected_date)

    def _load_routes(self) -> None:
        routes_path = self._path("routes.txt")
        for row in read_csv_dicts(routes_path):
            route = Route(
                index=len(self.routes),
                id=row["route_id"],
                agency_id=row.get("agency_id", ""),
                short_name=row.get("route_short_name", ""),
                long_name=row.get("route_long_name", ""),
                description=row.get("route_desc", ""),
                type=parse_int(row.get("route_type", ""), default=-1),
            )
            self.route_id_to_index[route.id] = route.index
            self.routes.append(route)

        self.counters["routes"] = len(self.routes)

    def _load_stops(self) -> None:
        stops_path = self._path("stops.txt")
        for row in read_csv_dicts(stops_path):
            stop = Stop(
                index=len(self.stops),
                id=row["stop_id"],
                code=row.get("stop_code", ""),
                name=row.get("stop_name", ""),
                lat=parse_float(row.get("stop_lat", "")),
                lon=parse_float(row.get("stop_lon", "")),
                platform_code=row.get("platform_code", ""),
                wheelchair_boarding=parse_optional_int(row.get("wheelchair_boarding", "")),
            )
            self.stop_id_to_index[stop.id] = stop.index
            self.stops.append(stop)

        self.counters["stops"] = len(self.stops)

    def _load_trips(self) -> None:
        trips_path = self._path("trips.txt")
        for row in read_csv_dicts(trips_path):
            service_id = row["service_id"]
            if not self._is_service_active(service_id):
                self.counters["trips_skipped_by_date"] += 1
                continue

            route_id = row["route_id"]
            route_index = self.route_id_to_index.get(route_id)
            if route_index is None:
                self.counters["trips_skipped_missing_route"] += 1
                continue

            if self.max_trips is not None and len(self.trips) >= self.max_trips:
                self.counters["trips_skipped_by_max_trips"] += 1
                continue

            trip = Trip(
                index=len(self.trips),
                id=row["trip_id"],
                route_index=route_index,
                route_id=route_id,
                service_id=service_id,
                headsign=row.get("trip_headsign", ""),
                direction_id=parse_optional_int(row.get("direction_id", "")),
                shape_id=row.get("shape_id", ""),
            )
            self.trip_id_to_index[trip.id] = trip.index
            self.trips.append(trip)

        self.counters["trips"] = len(self.trips)

        if self.selected_date is None:
            self.warnings.append(
                "Nie podano --date, więc JSON zawiera kursy ze wszystkich service_id. "
                "Do algorytmów wyszukiwania zwykle wygodniej wygenerować JSON dla konkretnej daty."
            )

    def _load_connections_from_stop_times(self) -> None:
        selected_trip_ids = set(self.trip_id_to_index.keys())
        stop_times_by_trip: Dict[str, List[StopEvent]] = defaultdict(list)

        stop_times_path = self._path("stop_times.txt")
        for row in read_csv_dicts(stop_times_path):
            trip_id = row["trip_id"]
            if trip_id not in selected_trip_ids:
                self.counters["stop_times_skipped_inactive_trip"] += 1
                continue

            stop_id = row["stop_id"]
            stop_index = self.stop_id_to_index.get(stop_id)
            if stop_index is None:
                self.counters["stop_times_skipped_missing_stop"] += 1
                continue

            event = StopEvent(
                stop_index=stop_index,
                stop_id=stop_id,
                arrival_seconds=parse_time_to_seconds(row.get("arrival_time", "")),
                departure_seconds=parse_time_to_seconds(row.get("departure_time", "")),
                stop_sequence=parse_int(row.get("stop_sequence", "")),
                pickup_type=parse_optional_int(row.get("pickup_type", "")),
                drop_off_type=parse_optional_int(row.get("drop_off_type", "")),
            )
            stop_times_by_trip[trip_id].append(event)

        for trip_id, events in stop_times_by_trip.items():
            if len(events) < 2:
                self.counters["trips_with_less_than_two_stop_times"] += 1
                continue

            events.sort(key=lambda event: event.stop_sequence)
            trip_index = self.trip_id_to_index[trip_id]
            trip = self.trips[trip_index]

            for previous, current in zip(events, events[1:]):
                departure = previous.departure_seconds
                arrival = current.arrival_seconds

                if arrival < departure:
                    # W GTFS czas po północy powinien być zapisany jako 24:xx:xx,
                    # 25:xx:xx itd. Jeżeli trafia się wartość mniejsza, to znaczy,
                    # że dane są niespójne dla tej krawędzi.
                    self.counters["connections_skipped_negative_time"] += 1
                    continue

                if previous.stop_index == current.stop_index:
                    self.counters["connections_skipped_same_stop"] += 1
                    continue

                connection = Connection(
                    from_stop=previous.stop_index,
                    to_stop=current.stop_index,
                    route=trip.route_index,
                    trip=trip_index,
                    departure=departure,
                    arrival=arrival,
                    travel_time=arrival - departure,
                    from_sequence=previous.stop_sequence,
                    to_sequence=current.stop_sequence,
                )
                self.connections.append(connection)

        self.connections.sort(
            key=lambda connection: (
                connection.from_stop,
                connection.departure,
                connection.arrival,
                connection.to_stop,
            )
        )
        self.counters["connections"] = len(self.connections)

    def _load_shapes(self) -> None:
        shapes_path = self._path("shapes.txt")
        if not shapes_path.exists():
            self.warnings.append("Podano --include-shapes, ale nie znaleziono shapes.txt.")
            return

        needed_shape_ids = {trip.shape_id for trip in self.trips if trip.shape_id}
        points_by_shape: Dict[str, List[ShapePoint]] = defaultdict(list)

        for row in read_csv_dicts(shapes_path):
            shape_id = row["shape_id"]
            if shape_id not in needed_shape_ids:
                self.counters["shape_points_skipped_unused_shape"] += 1
                continue

            point = ShapePoint(
                lat=parse_float(row.get("shape_pt_lat", "")),
                lon=parse_float(row.get("shape_pt_lon", "")),
                sequence=parse_int(row.get("shape_pt_sequence", "")),
                distance=parse_optional_float(row.get("shape_dist_traveled", "")),
            )
            points_by_shape[shape_id].append(point)

        for shape_id, points in sorted(points_by_shape.items()):
            points.sort(key=lambda point: point.sequence)
            self.shapes.append(Shape(id=shape_id, points=points))

        self.counters["shapes"] = len(self.shapes)
        self.counters["shape_points"] = sum(len(shape.points) for shape in self.shapes)

    def _build_json_document(self) -> Dict[str, Any]:
        routes_json = [asdict(route) for route in self.routes]
        stops_json = [asdict(stop) for stop in self.stops]
        trips_json = [asdict(trip) for trip in self.trips]
        connections_json = [
            {
                "from": connection.from_stop,
                "to": connection.to_stop,
                "route": connection.route,
                "trip": connection.trip,
                "departure": connection.departure,
                "arrival": connection.arrival,
                "travel_time": connection.travel_time,
                "from_sequence": connection.from_sequence,
                "to_sequence": connection.to_sequence,
            }
            for connection in self.connections
        ]

        document: Dict[str, Any] = {
            "metadata": self._build_metadata(),
            "services": [service.to_json_dict() for service in self.services.values()],
            "stops": stops_json,
            "routes": routes_json,
            "trips": trips_json,
            "connections": connections_json,
        }

        if self.include_adjacency:
            document["adjacency"] = self._build_adjacency_index()

        if self.include_shapes:
            document["shapes"] = [
                {
                    "id": shape.id,
                    "points": [asdict(point) for point in shape.points],
                }
                for shape in self.shapes
            ]

        return document

    def _build_adjacency_index(self) -> List[List[int]]:
        adjacency: List[List[int]] = [[] for _ in self.stops]
        for connection_index, connection in enumerate(self.connections):
            adjacency[connection.from_stop].append(connection_index)
        return adjacency

    def _build_metadata(self) -> Dict[str, Any]:
        files = [
            "agency.txt",
            "calendar.txt",
            "calendar_dates.txt",
            "feed_info.txt",
            "routes.txt",
            "stops.txt",
            "trips.txt",
            "stop_times.txt",
            "shapes.txt",
        ]

        return {
            "format": "gtfs-preprocessed-graph-json",
            "version": 1,
            "generated_at": datetime.now().isoformat(timespec="seconds"),
            "source_directory": str(self.gtfs_dir.resolve()),
            "selected_date": self.selected_date.isoformat() if self.selected_date else None,
            "include_shapes": self.include_shapes,
            "include_adjacency": self.include_adjacency,
            "max_trips": self.max_trips,
            "counts": dict(self.counters),
            "warnings": self.warnings,
            "source_files": {filename: file_stats(self._path(filename)) for filename in files},
            "time_encoding": "seconds_from_service_day_start; GTFS hours may exceed 23, e.g. 24:11:00",
            "graph_semantics": {
                "vertex": "stop",
                "edge": "connection between two consecutive stops within one trip",
                "directed": True,
                "time_dependent": True,
            },
        }


# -----------------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------------

def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Parsuje pliki GTFS-like do JSON-a przygotowanego pod graf w C++.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "gtfs_dir",
        type=Path,
        help="Folder zawierający calendar.txt, routes.txt, stops.txt, trips.txt, stop_times.txt itd.",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("graph.json"),
        help="Ścieżka wyjściowego pliku JSON. Jeśli kończy się na .gz, plik będzie skompresowany.",
    )
    parser.add_argument(
        "--date",
        type=str,
        default=None,
        help="Data rozkładowa w formacie YYYY-MM-DD. Jeśli podana, zostaną zapisane tylko aktywne kursy.",
    )
    parser.add_argument(
        "--include-shapes",
        action="store_true",
        help="Dołącza geometrię z shapes.txt. Przydatne do rysowania mapy, ale zwiększa rozmiar JSON-a.",
    )
    parser.add_argument(
        "--include-adjacency",
        action="store_true",
        help="Dołącza listę sąsiedztwa jako indeksy krawędzi. C++ może też zbudować ją sam z connections.",
    )
    parser.add_argument(
        "--max-trips",
        type=int,
        default=None,
        help="Limit kursów, przydatny do szybkich testów parsera na małej próbce.",
    )
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Zapisuje czytelny, wcięty JSON. Plik będzie większy.",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    selected_date = parse_iso_date(args.date) if args.date else None

    gtfs_parser = GtfsJsonParser(
        gtfs_dir=args.gtfs_dir,
        selected_date=selected_date,
        include_shapes=args.include_shapes,
        include_adjacency=args.include_adjacency,
        max_trips=args.max_trips,
    )

    try:
        gtfs_parser.export(args.output, pretty=args.pretty)
    except Exception as error:
        print(f"Błąd podczas parsowania GTFS: {error}", file=sys.stderr)
        return 1

    print(f"Zapisano JSON: {args.output}")
    print("Podsumowanie:")
    for key, value in sorted(gtfs_parser.counters.items()):
        print(f"  {key}: {value}")

    if gtfs_parser.warnings:
        print("Ostrzeżenia:")
        for warning in gtfs_parser.warnings:
            print(f"  - {warning}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
