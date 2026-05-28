# tsf-sdr

Software defined radio from a test suite, packaged as an external Test
Environment (TE) repository.

Library:

- `tapi_sdr` — engine-side, built as a shared library: record what a
  device transmits, decode it, compare two recordings, and send one
  back.

## What this is for

The protocol modules of this constellation each speak one language:
Bluetooth, LoRaWAN, MQTT. This one speaks none of them. It works a
layer below, on the radio itself, and that makes it the right tool for
the devices that have **no protocol worth the name** — a garage door, a
doorbell, a gate remote, a tyre sensor, a cheap thermometer.

For those, the test that finds something is nearly always the same one:
**record a transmission and send it again**. A device that acts on the
recording has no replay protection, and an astonishing number of them
do not. A device that is safe sends something different every time,
usually a rolling code; a device that is not sends the same few hundred
microseconds for its whole life, and anyone who recorded it once can
open the door for ever.

## Most of it is a file

A capture is a file of samples, and once it exists the radio is no
longer needed. Recording happens once; looking at it, decoding it and
comparing it to the next one happen as often as the test likes, on any
machine. A suite can record on a rig with hardware and run the analysis
anywhere.

Every function here either makes a capture, reads one, or sends one.

## Two ways to find a replay, and one is safe

**Comparing two recordings** — `tapi_sdr_replay_compare()` — puts
nothing on the air and needs no transmitter. Press the remote twice,
record both, and see whether a decoder reads the same thing out of
each. Two identical transmissions is the finding. This works with a
television-tuner dongle, which is what most rigs have.

**Sending the recording back** — `tapi_sdr_replay_send()` — is the
demonstration rather than the finding. It needs a radio that can
transmit and a great deal more care, and it is a separate function for
that reason.

| Finding | Severity | What it means |
|---|---|---|
| `sdr.repeats-itself` | high | two recordings of the same action decoded identically |
| `sdr.differs-between-sends` | info | they differed: a rolling code, probably |
| `sdr.nothing-decoded` | info | the decoder did not recognise this device |

The two `info` findings exist so the report cannot lie by silence.
`sdr.nothing-decoded` is the important one: the decoder knows several
hundred devices and may simply not know this one, and an absence of
findings would read as a device that is fine.

## Three backends, and the row that matters

| | rtl-sdr | HackRF | SoapySDR |
|---|---|---|---|
| find devices | yes | yes | yes |
| receive | yes | yes | yes |
| **transmit** | **no** | yes | depends |

An RTL-SDR dongle is a television tuner with a different driver on it.
There is no transmitter in it and no software will add one, so it can
find the problem and never demonstrate it. `tapi_sdr_transmit()` says
so rather than failing obscurely.

SoapySDR is a library over other radios, so what it can do is whatever
is plugged in. Receive is claimed because every device it supports can;
transmit is not, because claiming it would be a promise about hardware
this library has not seen.

## Detecting a radio, and two tools that lie

Measured against the tools as packaged, because neither is obvious:

- **`SoapySDRUtil --info` exits 0 with nothing attached at all.** It
  reports on its own modules, not on any hardware. Using it would make
  every agent look as though it had a radio. Only `--find` answers the
  question, and that exits 1 when there is nothing.
- **`hackrf_info` prints its version banner before discovering there is
  nothing there**, so the banner proves the program exists and nothing
  more. The serial number is what proves a radio does.

`rtl_test` is the honest one: `No supported devices found.` and exit 1.

## Transmitting is regulated

`tapi_sdr_transmit()` puts energy on the air. Into a cable or a
shielded box, on a frequency the rig is allowed to use. **This library
will not check, and cannot.** It logs a warning every time and that is
the whole of its contribution to the question.

## What was verified, and what was not

Held to the same standard as the rest of these repositories, which here
means being blunt: **no part of this was run against a radio.** There
is no SDR hardware on the machine it was written on.

What was measured, against the tools as packaged on Ubuntu 24.04:

- the behaviour of every tool with **no device attached** — the exit
  codes and the text above, which is what the detection and the skip
  path rest on;
- that `rtl_433 -r <file> -s <rate> -F json` reads a real `cu8`
  capture and exits 0;
- that a file it cannot decode anything from produces **exit 0 and
  silence**, so silence is an answer and not a failure — which is why
  `sdr.nothing-decoded` exists;
- that a missing file is different and says `Opening file ... failed!`.

What was **not** verified: the JSON field names `model` and `id` that
`tapi_sdr_decode()` reads. They are `rtl_433`'s documented output, and
no signal decodable on this machine could be produced to see them come
out. A synthetic capture was accepted by the decoder and recognised as
nothing, which tests the plumbing and not the parsing.

The first suite to point this at real hardware should expect to correct
something.

## Usage

```yaml
repositories:
  - name: tsf_sdr
    url: https://github.com/interpretica-io/tsf-sdr.git
    ref: <tag>
    libs: [ tapi_sdr ]
```

```
TE_EXT_REPO_USE([tsf_devtool], [], [tapi_devtool])
TE_EXT_REPO_USE([tsf_cybersec], [], [tapi_cybersec])
TE_EXT_REPO_USE([tsf_sdr], [], [tapi_sdr])
```

Then add `tapi_sdr` to `te_libs` in the suite's `meson.build`. On the
agent it needs `rtl-sdr` or `hackrf` or `soapysdr-tools`, and
`rtl-433` to decode anything.
