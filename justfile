# build docker
build_debug:
    docker build . -f dockerfiles/build_debug -t warthog_debug

bump:
    #!/bin/bash
    set -euo pipefail

    old=$(grep -oP "version : '\K[0-9.]+(?=')" meson.build)
    major=$(echo "$old" | cut -d. -f1)
    minor=$(echo "$old" | cut -d. -f2)
    patch=$(echo "$old" | cut -d. -f3)

    if [ "$patch" -ge 255 ]; then
        echo "ERROR: patch version cannot exceed 255" >&2
        exit 1
    fi

    new="${major}.${minor}.$((patch + 1))"
    sed -i "s/version : '${old}'/version : '${new}'/" meson.build
    echo "Bumped: ${old} -> ${new}"
