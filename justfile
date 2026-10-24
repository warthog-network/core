# build linux
build-linux:
    docker build . -f dockerfiles/build_linux --build-arg WARTHOG_COMMIT=$(shell git rev-parse --short HEAD) --output build

# build windows
build-windows:
    docker build . -f dockerfiles/build_windows --build-arg WARTHOG_COMMIT=$(shell git rev-parse --short HEAD) --output build

# bumps version
bump:
    #!/usr/bin/env bash
    set -euo pipefail

    old=$(grep -oP "version : '\K[0-9.]+(?=')" meson.build)
    major=$(echo "$old" | cut -d. -f1)
    minor=$(echo "$old" | cut -d. -f2)
    patch=$(echo "$old" | cut -d. -f3)

    if [ "$major" -ge 255 ] || [ "$minor" -ge 255 ] || [ "$patch" -ge 255 ]; then
        echo "ERROR: version components cannot exceed 255" >&2
        exit 1
    fi

    new="${major}.${minor}.$((patch + 1))"
    sed -i "s/version : '${old}'/version : '${new}'/" meson.build
    echo "Bumped: ${old} -> ${new}"

# runs valgrind
valgrind *args:
    #!/usr/bin/env bash
    # Usage: just valgrind -- [args for warthog]
    # Example: just valgrind -- --chain-db=/data/chain --other-flag
    # The -- separates Docker arguments from arguments passed to warthog inside the container
    # Everything after -- is forwarded to the warthog program running with valgrind
    set -euo pipefail

    chain_db_dir=""
    peers_db_dir=""

    for arg in {{ args }}; do
        case "$arg" in
            --chain-db=*)
                chain_db="${arg#--chain-db=}"
                chain_db_dir=$(dirname "$chain_db")
                ;;
            --peers-db=*)
                peers_db="${arg#--peers-db=}"
                peers_db_dir=$(dirname "$peers_db")
                ;;
        esac
    done

    img=$(docker build . -f dockerfiles/run_debug --build-arg WARTHOG_COMMIT=$(shell git rev-parse --short HEAD) -q)

    vg_args=()
    if [ -n "$chain_db_dir" ]; then
        vg_args+=("-v" "$chain_db_dir:/warthog/.warthog")
    fi
    if [ -n "$peers_db_dir" ] && [ "$peers_db_dir" != "$chain_db_dir" ]; then
        vg_args+=("-v" "$peers_db_dir:/warthog/.warthog_peers")
    fi

    docker run --rm -it --env VALGRIND_OPTS="--log-file=valgrind.log" "${vg_args[@]}" "$img" -- {{ args }}
