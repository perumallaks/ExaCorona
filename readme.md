# ExaCorona

_Kalyan Perumalla_

**Last updated**: _April 19, 2020_

- **Scalable Generator of Simulated Datasets for COVID and Similar Pandemics**

- **Tested on Linux, MacOS, and Summit Supercomputer**

- [https://github.com/perumallaks/ExaCorona](https://github.com/perumallaks/ExaCorona)

# Introduction

# Animations Video

Animations previously generated for publications cited below.  The animation tool previously used for these animations is in the process of being restored for ExaCorona.

[![Image](https://img.youtube.com/vi/HdQYkztNdVc/0.jpg)](https://youtu.be/HdQYkztNdVc)

# Overview Video

[![Image](https://img.youtube.com/vi/xRajXglguF8/0.jpg)](https://youtu.be/xRajXglguF8)

# Execution Video

[![Image](https://img.youtube.com/vi/zWukQnWxqIA/0.jpg)](https://youtu.be/zWukQnWxqIA)

# Quick Start

- `cd sim/src; make`
- `cd run; ./run.sh`
- `cd viz/simple; ./gendata.sh`
- `cd viz/simple; python3 ./plot.py`

# References

- **ACM-TOMACS'14** : "Discrete Event Execution with One-Sided and Two-Sided GVT Algorithms on 216,000 Processor Cores," Kalyan Perumalla, Alfred Park, and Vinod Tipparaju, ACM TOMACS 24(3), 2014 [DOI 10.1145/2611561](https://doi.org/10.1145/2611561)

- **SIMULATION'12** : "Discrete Event Modeling and Massively Parallel Execution of Epidemic Outbreak Phenomena," Kalyan Perumalla and Sudip Seal, SIMULATION: Transactions of the Society for Modeling and Simulation International, 2012 [DOI 10.1177/0037549711413001](https://doi.org/10.1177/0037549711413001)

# Scalability

ExaCorona is aimed at at least three dimensions of scalability:

- Level of detail in the model(s) behaviors.  **Examples**:
    - Evolution of disease: list of states (normal, latent, infectious, recovered, dead, etc.) and timed inter-state transition graph
    - Number of individuals or groups
    - Location-specific resources
    - Information propagation, policy enforncement, etc.
    - Transportation modes, mobility rules, etc.
    - Curfews, vaccination campaigns, quarantines, etc.
- Spatial scope and reach of the pandemic. **Examples**:
    - World-scale
    - Multiple countries
    - One country with multiple states
    - One state with multiple cities, etc.
    - Neighborhoods, types of locations
- Number of processors that can be employed to execute a scenario.  **Examples**:
    - Small laptop
    - Powerful desktop or server
    - Cluster
    - Racks
    - Supercomputers

# Repository Structure

- `sim` : Contains the simulated dataset generator proper

    - `sim/src` : Contains the simulated dataset generator proper
        - `exacorona.cpp` : Contains the discrete event model of virus spread
        - `musik` : Contains the parallel discrete event engine `musik`

    - `sim/bin` : The executable `exacorona` will be here upon compilation

- `data` : Houses the scenarios, each a sub-directory of its own

    - `data/example` : Contains an example scenario
        - `disease-normal.json` : Contains a state machine specification of disease
        - `scenario.json` : Contains the overall specification of the scenario
        - `geography.json`, `region*.json`, `location*.json` : Contains the details of the overall populations organized across the geography as regions, each region containing one or more locations.  The 'i'th region will be simulated by the 'i'th MPI rank (or, simply, 'i'th processor).

- `docs` : Houses copies of related publications

- `run` : Contains script(s) to execute the simulated dataset generator

    - `run/run.sh` : Change directory to `run` and execute `./run.sh` to kick-off the scenario `example.json`.

- `viz` : To house visualization tools.

    - [As of Apr 15, 2020: Contains `gendata.sh` and `plot.py`]

# Installation, Compilation, and Execution

## Compile the simulated dataset generator

-   Compile ExaCorona along with the underlying `musik` parallel simulation engine

      `cd sim/src; make`

    This creates the executable `sim/bin/exacorona`.

## Run the simulated dataset generator

-   Execute:

      `cd run; ./run.sh`

    - This runs `sim/bin/exacorona` with `run/example.json` as argument.

    - The `run/example.json` uses the scenario specified in `data/example`.

    - This generates multiple "log" files.
        - `stdout-0.log`: Standard output is redirected to this file.
        - `exacorona-0.log`: The disease model output is written to this file.
        - `musik-0.log`: The simulation engine's activity is logged to this file.
        - `tm-0.log`: The simulation engine's virtual time synchronization activity is logged to this file.
    - In parallel runs using multiple processors, each MPI rank (or simply, each processor) generates a file named with its rank.
    - For example, in a 2-processor run, `stdout-0.log` and `stdout-1.log` are created by processor 0 and processor 1, respectively.

## Plot some of the output

[Assuming you have `matplotlib` of Python]

`cd viz/simple; ./plot.sh`

This runs Python matplotlib-based plotting script to show the trend of infections over time in region 0.

# Installation on Linux/Ubuntu

## Ensure you can compile C/C++ with MPI

- Install C/C++, etc.  
  `sudo apt-get install build-essential`

- Install Message Passing Interface (MPI) support  
  `sudo apt install libopenmpi-dev`

## Ensure you can plot with Python's `matplotlib`

- Install `pip` if you don't already have:  
  `sudo apt install python3-pip`

- Install `matplotlib` if you don't already have:  
  `pip3 install matplotlib`

# Installation on MacOS

## Tested Configuration

- MacBook Pro
- macOS Cataline Version 10.15.4
- Xcode 11.4
- OpenMPI 2.0.4

## Installing XCode and OpenMPI

### Install Xcode

Install XCode from App Store, which will get commandline tools (C/C++ compiler, make, etc.).

### Install OpenMPI

```console
$ curl -O 'https://download.open-mpi.org/release/open-mpi/v2.0/openmpi-2.0.4.tar.gz'

$ tar -zxf openmpi-2.0.4.tar.gz
$ cd openmpi-2.0.4

# We will install at user-level locally
$ mkdir ~/opt ~/opt/usr ~/opt/usr/local
$ ./configure --prefix=$HOME/opt/usr/local

$ make all
$ make install

# Add the path to openmpi executables
$ export PATH=$PATH:$HOME/opt/usr/local/bin

# Verify that mpirun works
$ $HOME/opt/usr/local/bin/mpirun --version
$ $HOME/opt/usr/local/bin/mpirun -np 2 /bin/ls
```

## ExaCorona Compilation

Compilation, execution and visualization steps are similar to those on Linux.

# Installation on the OLCF Summit Supercomputer

## Ensure you can compile C++11

- In `sim/src/Makefile`, just add the flag needed to compile `C++11`.
    - For example, with GNU compilers, add `-std=gnu++11` to CFLAGS.
- The same `cd sim/src; make` works on Summit.

## Executing with batch submission

- Copy the executable `sim/bin/exacorona` to the work directory.
- Copy the `data/example` scenario directory to the work directory.
- Copy `run/example.json` to the work directory.
- Edit the `example.json` to set the correct path to the scenario directory.
- Make your job run the `exacorona` executable with command-line argument `example.json`.
- After the generator ends, copy the `exacorona.log` output file to where you want to process and visualize it.

# Executing large-scale scenarios in parallel

- Contact the [author(s)](mailto:kalyan.s.perumalla@gmail.com) before attempting large scenarios on many processors, in order to correctly plan and configure.

- This release is neither intended nor configured for runtime performance benchmarking or comparisons, so please refrain from reporting runtime data.  Please contact the [author(s)](mailto:kalyan.s.perumalla@gmail.com) if/as necessary.
