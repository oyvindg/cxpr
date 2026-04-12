/**
 * @file cxpr.h
 * @brief C API for cxpr expression evaluator.
 *
 * Pure C11 interface for maximum portability and FFI compatibility.
 */

#ifndef CXPR_H
#define CXPR_H

/** @brief Public cxpr major version. */
#define CXPR_VERSION_MAJOR 1
/** @brief Public cxpr minor version. */
#define CXPR_VERSION_MINOR 0
/** @brief Public cxpr patch version. */
#define CXPR_VERSION_PATCH 4

#include <cxpr/types.h>
#include <cxpr/ast.h>
#include <cxpr/context.h>
#include <cxpr/registry.h>
#include <cxpr/basket.h>
#include <cxpr/evaluator.h>
#include <cxpr/expression.h>

#endif /* CXPR_H */
