
# p4c-opt

P4c+ optimizations related to instruction scheduling


## Required software

- [DPDK 2.2.0](http://dpdk.org/download) - [Quick Start Quide](http://dpdk.org/doc/quick-start)
    - Note that P4@ELTE is currently **stable with DPDK 2.2.0**, the support for newer DPDK versions (such as 16.04.) is **experimental**.
    - The code may compile with earlier DPDK versions (such as 2.1.0).
- [P4 HLIR](https://github.com/p4lang/p4-hlir)

### Common pitfalls

- Do not forget to set the `RTE_SDK` environment variable to the directory of your DPDK.
- DPDK will need `pcap` header files (you can get them by installing `pcap`, e.g. the `libpcap-dev` package)
- You may also need the [setuptools Python package](https://pypi.python.org/pypi/setuptools) for a successful HLIR install.

## Running the examples

To execute the commands below, a root or sudoer account is necessary.
In the latter case, the system will ask for your password when you run it for the first time.

The most comfortable, all-inclusive choice is to run `launch.sh` (with proper parameters, see below). This will compile and run the switch in one go.

~~~~~~~~{.bash}
./launch.sh <P4 file> <controller> <controller params file> -- <DPDK parameters>
~~~~~~~~

- The `controller` is predefined for the examples, and can be omitted.
- You may have to use different DPDK settings to make the following examples work on your system.
- You can find more information about DPDK command line options [here](http://dpdk.org/doc/guides-16.04/testpmd_app_ug/run_app.html#eal-command-line-options).

~~~~~~~~{.bash}
## Test examples (will work without any real network configuration)

# using the L2 example without a real network configuration
P4DPDK_VARIANT=no_nic_l2 ./launch.sh ./examples/l2_switch_test.p4 -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""

# using the L3 example without a real network configuration
P4DPDK_VARIANT=no_nic_l3 ./launch.sh ./examples/l3_routing_test.p4 -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""

# the same as above, with verbose output
P4_GCC_OPTS="-DP4DPDK_DEBUG" P4DPDK_VARIANT=no_nic_l3 ./launch.sh ./examples/l3_routing_test.p4 -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""


## The following examples will only work if you have a real network configuration with DPDK

# supplying DPDK options directly
./launch.sh examples/l3_routing_test.p4 -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""

# supplying DPDK options via environment variable
export P4DPDK_OPTS="-c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config \"(0,0,0),(1,0,1)\""
./launch.sh examples/l3_routing_test.p4

# using the L3 example with a configuration file for the control plane
# and manual specification of the controller ("dpdk_controller")
./launch.sh ./examples/l3_routing_test.p4 dpdk_controller examples/l3_switch_test_ctrl.txt -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""

# verbose output
# "default" means that we use the default controller (applicable to example programs only)
P4_GCC_OPTS="-DP4DPDK_DEBUG" ./launch.sh ./examples/l3_routing_test.p4 default -- -c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config "\"(0,0,0),(1,0,1)\""
~~~~~~~~


## Running the examples, details

You can `compile`, `make` and `run` the code in separate steps.

1.  Compile the P4 file into C code using `compile.sh` and setup files for `make`.
    - The compiled files are placed within the directory `build/<P4-source-name>`.

    ~~~~~~~~{.bash}
    ./compile.sh examples/l3_routing_test.p4
    ~~~~~~~~

1.  Run `make` in the directory `build/<P4-source-name>`.
    - The executable file is generated as `build/<P4-source-name>/build/<P4-source-name>`.
1.  Run the executable.
    - If you prefer to do so, you can invoke the generated executable directly.
    - As the executable will require a controller to function,
      there is a convenience script `run.sh` that you can invoke.
      For each example, the appropriate controller is started before running the switch.
    - The parameters for `run.sh` are the same as for `launch.sh`
      except for the first one: here, you have to supply the name of the generated executable.

    ~~~~~~~~{.bash}
    ./run.sh ./build/l3_routing_test/build/l3_routing_test -- -c 0xf -n 1 --log-level 3 -- -P -p 0x3

    # supplying DPDK options via environment variable
    export P4DPDK_OPTS="-c 0x3 -n 4 - --log-level 3 -- -p 0x3 --config \"(0,0,0),(1,0,1)\""
    ./run.sh ./build/l3_routing_test/build/l3_routing_test
    ~~~~~~~~


## Environment variables

You can set the following parameters via environment variables.

- In general, for DPDK
    - `RTE_SDK`: the directory of DPDK
    - `RTE_TARGET`: the target architecture, will default to `x86_64-native-linuxapp-gcc` if unspecified
- Relevant in the `make` step
    - `MAKE_CMD`
        - You can specify a program other than `make` to be invoked.
    - `P4DPDK_GCC_OPTS`
        - You can specify additional parameters for your compiler.
        - To see debug messages, include `-NP4DPDK_DEBUG` in its contents.
    - `P4DPDK_VARIANT`
        - Possible values are `no_nic_l2` and `no_nic_l3` (or leave it unset).
        - Causes `src/hardware_dep/dpdk/main_loop_<variant>.c` to replace `src/hardware_dep/dpdk/main_loop.c` during compilation.
        - Mainly used for testing purposes, for DPDK installations without a real network environment.
- Relevant in `run.sh`
    - `P4DPDK_OPTS`: DPDK options for the compiled switch
        - If set, this overrides the parameters given after the first `--` on the command line of `launch.sh`.

## Limitations

The latest release (`1607`) implements the P4 language version 1.0. We'll support the latest versions when they become stable. Please note that the following language elements are not (fully) supported yet:
 - calculated/variable length fields
 - saturating fields
 - masked field modification
 - action profiles
 - parser exceptions
 - checksums
 - meters and registers
 - resubmission and cloning


## Experimental branch

The `experimental` branch contains preliminary implementations of features.
Currently, the branch contains the following:

- registers
    - registers bigger than 32 bits are not supported
    - registers with layout or binding are not supported
