/**
 * @file limits.h
 * @brief Shared internal hard limits for cxpr runtime and IR helpers.
 */

#ifndef CXPR_LIMITS_H
#define CXPR_LIMITS_H

/** @brief Maximum supported argument count for one call site during binding/codegen. */
#define CXPR_MAX_CALL_ARGS 32
/** @brief Maximum number of parameter names tracked for one callable signature. */
#define CXPR_MAX_PARAM_NAMES 64
/** @brief Maximum number of fields exposed by one struct producer. */
#define CXPR_MAX_PRODUCER_FIELDS 64

/** @brief Recursion limit used when deciding whether IR inlining stays tractable. */
#define CXPR_IR_INLINE_DEPTH_LIMIT 8
/** @brief Recursion limit used by IR type/result inference helpers. */
#define CXPR_IR_INFER_DEPTH_LIMIT 32
/** @brief Fixed operand-stack capacity for IR interpreters and validators. */
#define CXPR_IR_STACK_CAPACITY 64

#endif /* CXPR_LIMITS_H */
