FROM ubuntu:jammy
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y build-essential      \
                       gcc-11               \
                       g++-11               \
                       git                  \
                       python3-pip
RUN pip install -v "conan==2.15.1"
