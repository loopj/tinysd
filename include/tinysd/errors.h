#pragma once

/**
 * Errors the SD stack returns, from the controller up through the card.
 */
enum tsd_error {
  /** A command or data phase timed out. */
  TSD_ERR_TIMEOUT = -1,

  /** A command failed. */
  TSD_ERR_COMMAND = -2,

  /** A data phase failed, a CRC error among them. */
  TSD_ERR_DATA = -3,

  /** The bus stayed busy with an earlier command. */
  TSD_ERR_BUSY = -4,

  /** A parameter was out of range. */
  TSD_ERR_PARAM = -5,

  /** The bus power did not come up. */
  TSD_ERR_POWER = -6,

  /** The card reported an error in its status, such as a write to a protected block. */
  TSD_ERR_CARD = -7,
};
