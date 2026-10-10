..
   Copyright (c) 2026 Muhammad Waleed Badar
   SPDX-License-Identifier: GPL-3.0-only

Hardware
########

VIEWE UEDX32480035E-WB-A
************************

- ESP32-S3 with 16 MB flash and 8 MB octal PSRAM
- 3.5" 480x320 ST7796S display with CHSC6X touch controller, used in
  landscape
- Backlight enable on GPIO13, exposed to the player as ``zspot-backlight``
- No audio DAC: an external I2S DAC is required

The board's overlays are ``app/boards/uedx32480035e_wb_a_esp32s3_procpu.overlay``
(display and touch orientation, backlight) and
``app/socs/esp32s3_procpu.overlay`` (I2S pins).

I2S pins
********

.. list-table::
   :header-rows: 1

   * - Signal
     - GPIO
     - Pinctrl
   * - Data
     - GPIO5
     - ``I2S0_O_SD_GPIO5``
   * - Bit clock
     - GPIO6
     - ``I2S0_O_BCK_GPIO6``
   * - Word select (LRCK)
     - GPIO7
     - ``I2S0_O_WS_GPIO7``

No master clock is used. The firmware sends 16-bit stereo at 44.1 kHz.

MAX98357 amplifier
******************

The MAX98357 is a class-D amplifier with an I2S input that drives a speaker
directly.

.. list-table::
   :header-rows: 1
   :widths: 20 25 55

   * - MAX98357 pin
     - Connect to
     - Notes
   * - DIN
     - GPIO5
     - Audio data
   * - BCLK
     - GPIO6
     - Bit clock
   * - LRC
     - GPIO7
     - Word select
   * - GND
     - GND
     - Common ground with the board
   * - Vin
     - 5 V or 3.3 V
     - 2.5 to 5.5 V; 5 V gives the most output power
   * - OUT+ / OUT-
     - Speaker
     - 4 to 8 Ω directly across the two; never connect either to ground
   * - GAIN
     - Unconnected
     - Default gain; tie to GND or Vin to change it
   * - SD
     - Unconnected
     - Mono mix of both channels on the usual breakouts; to Vin for the left
       channel only; low mutes

.. note::

   ``SD`` in the pinctrl names means *serial data* and goes to the amplifier's
   ``DIN``. The amplifier's own ``SD`` pin is *shutdown* and must not be
   connected to GPIO5.

Volume is applied in software by the I2S sink, so ``GAIN`` only sets the
maximum loudness.

Other boards
************

The player needs IPv4 networking, an I2S device behind the devicetree alias
``zspot-i2s``, a display with touch for the UI, and enough RAM; see
:doc:`getting_started`. The ``native_sim`` target shows the minimum set of
overlays and options for a new board.

A codec that has to be set up over I2C or SPI (one with a Zephyr audio codec
driver) goes behind the optional alias ``zspot-codec``, with
``CONFIG_AUDIO_CODEC=y``. The sink configures it for the I2S format, with the
SoC driving the clocks, and mutes it while paused. Volume stays in software.
