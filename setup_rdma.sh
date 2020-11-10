
#installing library for rdma

echo y | sudo apt-get install build-essential cmake 
libudev-dev libnl-3-dev libnl-route-3-dev 
pkg-config valgrind python3-dev cython3 python3-docutils pandoc

git clone https://github.com/linux-rdma/rdma-core.git
cd rdma-core
sudo bash build.sh


apt-get install libtool autoconf automake  ibverbs-utils rdmacm-utils infiniband-diags perftest
librdmacm-dev libibverbs-dev numactl libnuma-dev libaio-dev libevent-dev

