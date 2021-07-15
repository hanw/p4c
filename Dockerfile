FROM p4lang/behavioral-model:latest

# Default to using 2 make jobs, which is a good default for CI. If you're
# building locally or you know there are more cores available, you may want to
# override this.
ARG MAKEFLAGS=-j2

# Select the type of image we're building. Use `build` for a normal build, which
# is optimized for image size. Use `test` if this image will be used for
# testing; in this case, the source code and build-only dependencies will not be
# removed from the image.
ARG IMAGE_TYPE=build
# Whether to do a unified build.
ARG ENABLE_UNIFIED_COMPILATION=ON
# Whether to enable translation validation
ARG VALIDATION=OFF
# This creates a release build that includes link time optimization and links
# all libraries statically.
ARG BUILD_STATIC_RELEASE=OFF
# Build dpdk app for p4c-dpdk testing
ARG DPDK=OFF

# # Force /usr/bin to be before /usr/local/bin because bf-driver
# # installs a version of python3 in /usr/local and that version is not
# # picking up modules installed with pip3
# ENV PATH="/usr/bin:${PATH}"
#
# # Set up Intel proxies.
# ENV http_proxy='http://proxy-dmz.intel.com:911'
# ENV https_proxy='http://proxy-dmz.intel.com:912'
# ENV ftp_proxy='http://proxy-dmz.intel.com:21'
# ENV socks_proxy='proxy-dmz.intel.com:1080'
# ENV no_proxy='intel.com,*.intel.com,localhost,127.0.0.1,10.0.0.0/8,192.168.0.0/16,172.16.0.0/12'
# ENV ALL_PROXY='socks5://proxy-us.intel.com'
#
# Dclegate the build to tools/ci-build.
COPY . /p4c/
WORKDIR /p4c/
RUN chmod u+x tools/ci-build.sh && tools/ci-build.sh

# setup huge pages
ENTRYPOINT ["/bfn/docker_entry_point.sh"]
