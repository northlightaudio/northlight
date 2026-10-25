# Blender for headless renders on an NVIDIA GPU (Cycles, OptiX or CUDA).
# Northlight's face renders (scripts/render/*): the version the scripts are written for.
FROM ubuntu:24.04
ARG BLENDER_VERSION=4.2.3
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl xz-utils libxrender1 libxi6 libxkbcommon0 libxxf86vm1 libxfixes3 \
        libxext6 libx11-6 libsm6 libice6 libgl1 libegl1 libglu1-mesa libfreetype6 \
    && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "https://download.blender.org/release/Blender4.2/blender-${BLENDER_VERSION}-linux-x64.tar.xz" \
        | tar -xJ -C /opt && ln -s /opt/blender-${BLENDER_VERSION}-linux-x64/blender /usr/local/bin/blender
WORKDIR /work
ENTRYPOINT ["blender", "--background", "--factory-startup"]
