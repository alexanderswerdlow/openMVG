# Use Ubuntu 18.04 (will be supported until April 2023)
FROM ubuntu:bionic

# Add openMVG binaries to path
ENV PATH $PATH:/opt/openmvg_build/install/bin

# Get dependencies
RUN apt-get update && apt-get install -y \
  cmake \
  build-essential \
  graphviz \
  git \
  coinor-libclp-dev \
  libceres-dev \
  libflann-dev \
  liblemon-dev \
  libjpeg-dev \
  libpng-dev \
  libtiff-dev \
  python-minimal \
  g++ \
  wget \
  unzip; \
  apt-get autoclean && apt-get clean

ARG threads=10
WORKDIR "/opt"
# Download and unpack sources
RUN wget -O opencv.zip https://github.com/opencv/opencv/archive/master.zip
RUN wget -O opencv_contrib.zip https://github.com/opencv/opencv_contrib/archive/master.zip
RUN unzip opencv.zip
RUN unzip opencv_contrib.zip

RUN apt-get install python3-pip python3-dev python3-numpy python-dev python-numpy libjpeg-dev libpng-dev -y
# Create build directory and switch into it
RUN mkdir -p opencv_build && cd opencv_build && cmake -DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib-master/modules ../opencv-master
RUN cd opencv_build && cmake --build . -- -j $threads
RUN rm opencv.zip && rm opencv_contrib.zip

ADD requirments.txt /tmp
RUN pip install -r /tmp/requirments.txt

# Clone the openvMVG repo
ADD . /opt/openMVG
RUN cd /opt/openMVG && git submodule update --init --recursive

WORKDIR "/opt"
# Build
RUN mkdir openmvg_build; \
  cd openmvg_build; \
  cmake -DCMAKE_BUILD_TYPE=RELEASE \
    -DOpenMVG_USE_OPENCV=ON \
    -DOpenCV_DIR="/opt/opencv_build" \
    -DCMAKE_INSTALL_PREFIX="/opt/openmvg_build/install" \
    -DOpenMVG_BUILD_TESTS=ON \
    -DOpenMVG_BUILD_EXAMPLES=ON \
    -DFLANN_INCLUDE_DIR_HINTS=/usr/include/flann \
    -DLEMON_INCLUDE_DIR_HINTS=/usr/include/lemon \
    -DCOINUTILS_INCLUDE_DIR_HINTS=/usr/include \
    -DCLP_INCLUDE_DIR_HINTS=/usr/include \
    -DOSI_INCLUDE_DIR_HINTS=/usr/include \
    ../openMVG/src

WORKDIR "/opt/openmvg_build"
RUN cmake --build . --target install -- -j $threads