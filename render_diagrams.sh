#!/usr/bin/env sh
set -eu

mkdir -p diagrams/out

# Render all PlantUML files in Diagrams/ to SVG in Diagrams/out/
docker run --rm -v "$PWD/diagrams:/work" plantuml/plantuml:latest \
  -tsvg -o out /work/*.plantuml

echo "Rendered diagrams to diagrams/out/"
ls -1 diagrams/out || true
