## OpenDLV microservice to interface with RTSP/RTP cameras

This repository provides source code to interface with cameras providing a
video or audio stream over RTSP/RTP for the OpenDLV software ecosystem. This 
microservice provides the captured frames as ImageReading messages.

[![License: GPLv3](https://img.shields.io/badge/license-GPL--3-blue.svg
)](https://www.gnu.org/licenses/gpl-3.0.txt)


## Table of Contents
* [Dependencies](#dependencies)
* [Usage](#usage)
* [Build from sources on the example of Ubuntu 16.04 LTS](#build-from-sources-on-the-example-of-ubuntu-1604-lts)
* [License](#license)


## Dependencies
You need a C++14-compliant compiler to compile this project. The following
dependency is shipped as part of the source distribution:

* [libcluon](https://github.com/chrberger/libcluon) - [![License: GPLv3](https://img.shields.io/badge/license-GPL--3-blue.svg
)](https://www.gnu.org/licenses/gpl-3.0.txt)
* [libcurl](https://github.com/curl/curl) - [License](https://github.com/curl/curl/blob/master/COPYING)


## Usage
This microservice is created automatically on changes to this repository via Docker's public registry for:
* [x86_64](https://hub.docker.com/r/chalmersrevere/opendlv-device-camera-rtp-amd64/tags/)
* [armhf](https://hub.docker.com/r/chalmersrevere/opendlv-device-camera-rtp-armhf/tags/)
* [aarch64](https://hub.docker.com/r/chalmersrevere/opendlv-device-camera-rtp-aarch64/tags/)

To run this microservice using our pre-built Docker multi-arch images, simply 
start it as follows:

```
docker run --rm -ti --init --ipc=host --net=host chalmersrevere/opendlv-device-camera-rtp-multi:v0.0.4 --url=rtsp://10.42.42.128/axis-media/media.amp?camera=1 --cid=102 --id=0 --client-port-udp-a=35000 --name=abc --verbose --remote --recsuffic=-rtp
```


## License

* This project is released under the terms of the GNU GPLv3 License

