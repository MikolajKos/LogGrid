# ====================================================
# BUILDER (intermediate image)
# ====================================================

FROM ubuntu:26.04 AS builder

# set a directory for the app
WORKDIR /usr/src/app

RUN apt-get update && apt-get install -y cmake g++ make ninja-build git

# move contents of current dir to chosen workdir
COPY . .

# build project
RUN mkdir build && cd build && cmake .. -G "Ninja" && ninja

# ====================================================
# WORKER IMAGE
# ====================================================

FROM ubuntu:26.04
WORKDIR /usr/src/app
# copy only worker executable
COPY --from=builder /usr/src/app/build/NetClient/NetClientApp .

# start client on docker run
CMD ["./NetClientApp"]