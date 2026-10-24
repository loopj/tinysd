#pragma once

/**
 * One bit at a position.
 *
 * @param n the bit's position, 0 to 31
 */
#define TSD_BIT(n) (1u << (n))

/**
 * A field's mask, from its high bit down to its low bit, as the specifications number them.
 *
 * @param high the field's highest bit, 0 to 31
 * @param low the field's lowest bit, at most high
 */
#define TSD_BITS(high, low) ((~0u >> (31 - (high))) & ~((1u << (low)) - 1u))

/**
 * Take a field's value out of a register word.
 *
 * @param field the field's mask, from TSD_BIT or TSD_BITS
 * @param word the register word
 */
#define TSD_FIELD_GET(field, word) (((word) & (field)) / ((field) & -(field)))

/**
 * Place a value into a field, ready to OR into a register word.
 *
 * @param field the field's mask, from TSD_BIT or TSD_BITS
 * @param value the value, cut to the field's width
 */
#define TSD_FIELD_PREP(field, value) (((value) * ((field) & -(field))) & (field))
