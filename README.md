# xrdp-broker
This repository is a fork of xrdp, that provides functionality for a load-balancing broker.

## Requirements
This solution only works with the Kerberos authentication method set up through PAM.
**Every server needs to have the same Kerberos setup for this to work correctly.**

## Compilation
This solution was only tested and built in Debian/Ubuntu. This is how to set up xrdp-broker on Debian/Ubuntu systems:
First, you need to install the dependencies:
```
wget https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/tags/v${XRDP_VERSION}/scripts/install_xrdp_build_dependencies_with_apt.sh
chmod +x install_xrdp_build_dependencies_with_apt.sh
sudo ./install_xrdp_build_dependencies_with_apt.sh max
```

Then, we need to configure xrdp to include specific libraries that this solution uses:
```
./bootstrap
./configure CFLAGS="-Wno-deprecated-declarations" LIBS="-ljansson -lnng"
make
```
and then install the xrdp server on your system:
```
sudo make install
sudo ln -s /usr/local/sbin/xrdp{,-sesman} /usr/sbin
```

## Setup
To run xrdp-broker, you need to execute the agent, located in `./tools/broker`, on each server you want the broker to manage.
To do this, you need to execute the agent on each server with the address you are intending the broker to run on, and the hostname of the server itself:
First, change into the directory of the broker and agent:
```
cd ./tools/broker
```
Then, execute the agent itself:

```
./agent <broker_address:port> <hostname of current server>
```

Then, when every agent has started, you can run the broker by running:
```
./broker
```
in the same folder.
