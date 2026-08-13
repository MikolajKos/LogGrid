# ====================================================
# BUILDER (intermediate image)
# ====================================================

FROM ubuntu:26.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# set a directory for the app
WORKDIR /usr/src/app

RUN apt-get update && apt-get install -y cmake g++ make ninja-build git

# move contents of current dir to chosen workdir
COPY . .

# build project
RUN mkdir build && cd build && cmake .. -G "Ninja" && ninja

# ====================================================
# MASTER IMAGE
# ====================================================

FROM ubuntu:26.04 AS master_image

WORKDIR /usr/src/app
# copy only master executable
COPY --from=builder /usr/src/app/build/NetServer/NetServerApp .

# start server on docker run
CMD ["./NetServerApp"]

# ====================================================
# WORKER IMAGE
# ====================================================

FROM ubuntu:26.04 AS worker_image

WORKDIR /usr/src/app
# copy only worker executable
COPY --from=builder /usr/src/app/build/NetClient/NetClientApp .

# start client on docker run
CMD ["./NetClientApp"]

# ====================================================
# TEST IMAGE
# ====================================================

FROM ubuntu:26.04 AS test_image

WORKDIR /usr/src/app
# copy only test binary
COPY --from=builder /usr/src/app/build/tests/LogGridTests .

CMD ["./LogGridTests"]

# ====================================================
# COVERAGE IMAGE
# ====================================================

FROM ubuntu:26.04 AS coverage_builder

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /usr/src/app

RUN apt-get update && apt-get install -y cmake g++ make ninja-build git lcov

COPY . .

RUN mkdir build && cd build \
    && cmake .. -G "Ninja" -DENABLE_COVERAGE=ON \
    && ninja LogGridTests

FROM coverage_builder AS coverage_image

WORKDIR /usr/src/app/build

CMD ["cmake", "--build", ".", "--target", "coverage"]
