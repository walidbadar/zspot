..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

Getting started
###############

Requirements
************

- Zephyr 4.5 and a Zephyr SDK with a full libstdc++ (C++20).
- ``protoc`` on the ``PATH`` at build time. Without a system package:

  .. code-block:: sh

     python3 -m venv ~/.venvs/protoc && ~/.venvs/protoc/bin/pip install grpcio-tools
     printf '#!/bin/sh\nexec "$(dirname "$0")/python" -m grpc_tools.protoc "$@"\n' > ~/.venvs/protoc/bin/protoc
     chmod +x ~/.venvs/protoc/bin/protoc
     export PATH=~/.venvs/protoc/bin:$PATH

- A board with IPv4 networking and about 300 KB of heap for the protocol.
  With PSRAM (``CONFIG_ZSPOT_EXTERNAL_HEAP``) most of it comes from external
  memory.

Setting up a workspace
**********************

The repository is a west manifest repository and a Zephyr module. Either
create a workspace from it:

.. code-block:: sh

   west init -m https://github.com/walidbadar/zspot --mr main zspot-workspace
   cd zspot-workspace && west update

or link it into an existing workspace:

.. code-block:: sh

   cd ~/zephyrproject
   ln -s /path/to/zspot zspot
   west config manifest.path zspot
   west update zephyr

To use only the library from another project, add the repository to that
project's west manifest or pass ``-DZEPHYR_EXTRA_MODULES=/path/to/zspot``.

Repository layout
*****************

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Path
     - Contents
   * - ``app/``
     - The player application: UI, Wi-Fi, library view, lyrics, battery
   * - ``lib/zspot/``
     - The protocol library: Spotify protocol core, Zephyr port layer,
       C API, I2S sink, Tremor Vorbis decoder
   * - ``include/zspot/``
     - Public headers of the library
   * - ``drivers/``, ``dts/``
     - Out-of-tree drivers and bindings, e.g. the ``native_sim`` fuel gauge
   * - ``scripts/``
     - Web API authorisation, zeroconf hand-over emulator, ``native_sim``
       LAN bridge
   * - ``tests/``
     - Twister test suites
   * - ``doc/``
     - This documentation

Building the documentation
**************************

.. code-block:: sh

   pip install -r doc/requirements.txt
   sphinx-build -b html doc doc/_build/html

Then open ``doc/_build/html/index.html``.
