/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Software defined radio from a test
 *
 * @defgroup tapi_sdr Software defined radio (tapi_sdr)
 * @{
 *
 * Record what a device transmits, look at it, and send it again.
 *
 * @section tapi_sdr_why What this is for
 *
 * The protocol modules of this constellation each speak one language:
 * @ref tapi_bt Bluetooth, @ref tapi_lorawan LoRaWAN, @ref tapi_mqtt
 * MQTT. This one speaks none of them. It works a layer below, on the
 * radio itself, and that is what makes it the right tool for the
 * devices that have no protocol worth the name - a garage door, a
 * doorbell, a tyre sensor, a remote that sends the same few hundred
 * microseconds of on-off keying every time.
 *
 * For those, the test that finds something is nearly always the same
 * one: **record a transmission and send it again**. A device that acts
 * on the recording has no replay protection, and an astonishing number
 * of them do not. @ref tapi_sdr_replay is that test.
 *
 * @section tapi_sdr_file Most of it is a file
 *
 * A capture is a file of samples, and once it exists the radio is no
 * longer needed. Recording happens once; looking at it, decoding it
 * and comparing it to the next one happen as often as the test likes,
 * on any machine.
 *
 * That shapes the whole library: every function here either makes a
 * capture, reads one, or sends one. A suite can record on a rig with
 * hardware and run the analysis anywhere.
 *
 * @section tapi_sdr_backends Three backends, and what each can do
 *
 * | | rtl-sdr | HackRF | SoapySDR |
 * |---|---|---|---|
 * | find devices | yes | yes | yes |
 * | receive | yes | yes | yes |
 * | **transmit** | **no** | yes | depends |
 *
 * The first row of that table is the one that decides whether a replay
 * test is possible at all. An RTL-SDR dongle is a television tuner: it
 * can listen to anything and say nothing, so it can find the problem
 * and never demonstrate it.
 *
 * @code
 * tapi_sdr_capture_opt opt = tapi_sdr_capture_default_opt;
 *
 * opt.frequency_hz = 433920000;
 * opt.seconds = 5;
 * CHECK_RC(tapi_sdr_capture(factory, &opt, "/tmp/doorbell.cu8", 30000));
 * @endcode
 *
 * @note Transmitting is regulated. A test that sends anything is
 *       responsible for doing it into a cable or a shielded box, on a
 *       frequency it is allowed to use. This library will not check.
 */

#ifndef __TSF_TAPI_SDR_H__
#define __TSF_TAPI_SDR_H__

#include <stdint.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default timeout for one radio operation, ms. */
#define TAPI_SDR_TIMEOUT_MS 60000

/** Which radio, and which program drives it. */
typedef enum tapi_sdr_backend {
    /** Ask the agent what it has. */
    TAPI_SDR_AUTO = 0,
    /** An RTL-SDR dongle: @c rtl_sdr. Receive only. */
    TAPI_SDR_RTL,
    /** A HackRF: @c hackrf_transfer. Receives and transmits. */
    TAPI_SDR_HACKRF,
    /** Anything SoapySDR knows: @c SoapySDRUtil and @c rx_tools. */
    TAPI_SDR_SOAPY,
} tapi_sdr_backend;

/** The radio can be listened to. */
#define TAPI_SDR_FEAT_RECEIVE  (1u << 0)
/** The radio can be transmitted from. */
#define TAPI_SDR_FEAT_TRANSMIT (1u << 1)

/** How to record. */
typedef struct tapi_sdr_capture_opt {
    /** Which radio; @ref TAPI_SDR_AUTO to ask the agent. */
    tapi_sdr_backend backend;
    /** Centre frequency in hertz. Required. */
    uint64_t frequency_hz;
    /** Sample rate in hertz; @c 0 means 2 048 000. */
    uint32_t sample_rate_hz;
    /** Gain in decibels; @c 0 means let the radio decide. */
    unsigned int gain_db;
    /** How long to record; @c 0 means until the sample count is met. */
    unsigned int seconds;
    /** How many samples; @c 0 means until the time is up. */
    uint32_t samples;
    /** Index of the device when there is more than one. */
    unsigned int device_index;
    /** Extra arguments, for what this does not wrap. */
    const char **extra_args;
    /** Number of @a extra_args. */
    size_t n_extra_args;
} tapi_sdr_capture_opt;

/** Defaults: 2.048 MHz, automatic gain, five seconds. */
extern const tapi_sdr_capture_opt tapi_sdr_capture_default_opt;

/**
 * Is there a radio the agent can use?
 *
 * Reads what the tools print rather than trusting their exit status,
 * because two of them lie about it. Measured:
 *
 * - @c SoapySDRUtil @c --info exits **0 with no device attached** - it
 *   is reporting on its own modules, not on any hardware - so only
 *   @c --find answers the question;
 * - @c hackrf_info prints its version banner before discovering there
 *   is nothing there, so the banner is not an answer either.
 *
 * @param factory       Job factory.
 * @param backend       Which to look for, or @ref TAPI_SDR_AUTO.
 * @param timeout_ms    Timeout, ms.
 *
 * @return @c true when a radio answered.
 */
extern bool tapi_sdr_available(tapi_job_factory_t *factory,
                               tapi_sdr_backend backend, int timeout_ms);

/**
 * Work out which radio the agent has.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] backend      What was found.
 *
 * @return Status code.
 * @retval TE_ENOENT        There is no radio.
 */
extern te_errno tapi_sdr_detect(tapi_job_factory_t *factory,
                                int timeout_ms,
                                tapi_sdr_backend *backend);

/**
 * What a backend can do.
 *
 * @param backend       Backend.
 *
 * @return A mask of @c TAPI_SDR_FEAT_*.
 */
extern unsigned int tapi_sdr_features(tapi_sdr_backend backend);

/**
 * Can this backend do this?
 *
 * @param backend       Backend.
 * @param feature       One or more @c TAPI_SDR_FEAT_*.
 *
 * @return @c true when all of @p feature are supported.
 */
extern bool tapi_sdr_supports(tapi_sdr_backend backend,
                              unsigned int feature);

/**
 * Record to a file on the agent.
 *
 * The file holds interleaved unsigned eight-bit samples - the format
 * every one of these tools calls @c cu8 - so a capture made with one
 * radio can be read by a decoder built for another.
 *
 * @param factory       Job factory.
 * @param opt           What to record.
 * @param path          Where to write it on the agent.
 * @param timeout_ms    Timeout, ms. Give it more than @a seconds.
 *
 * @return Status code.
 * @retval TE_ENODEV    There is no radio.
 */
extern te_errno tapi_sdr_capture(tapi_job_factory_t *factory,
                                 const tapi_sdr_capture_opt *opt,
                                 const char *path, int timeout_ms);

/**
 * Send a recorded file back out.
 *
 * @note This puts energy on the air. Into a cable or a shielded box,
 *       on a frequency the rig is allowed to use - this library will
 *       not check, and cannot.
 *
 * @param factory       Job factory.
 * @param opt           Frequency, rate and gain to send at.
 * @param path          The file on the agent.
 * @param timeout_ms    Timeout, ms.
 *
 * @return Status code.
 * @retval TE_EOPNOTSUPP    The radio cannot transmit; an RTL-SDR
 *                          dongle is a television tuner and never
 *                          will.
 */
extern te_errno tapi_sdr_transmit(tapi_job_factory_t *factory,
                                  const tapi_sdr_capture_opt *opt,
                                  const char *path, int timeout_ms);

/**
 * Spell out a backend.
 *
 * @param backend       Backend.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_sdr_backend2str(tapi_sdr_backend backend);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_SDR_H__ */

/**@} <!-- END tapi_sdr --> */
