/**
 * @file cxpr.h
 * @brief C API for cxpr expression and model compiler infrastructure.
 *
 * Pure C11 interface for maximum portability and FFI compatibility.
 */

#ifndef CXPR_H
#define CXPR_H

#include <cxpr/version.h>
#include <cxpr/types.h>
#include <cxpr/token.h>
#include <cxpr/ast.h>
#include <cxpr/parser.h>
#include <cxpr/analysis.h>
#include <cxpr/eval.h>
#include <cxpr/eval_snapshot.h>
#include <cxpr/program.h>
#include <cxpr/codegen.h>
#include <cxpr/model.h>
#include <cxpr/alias.h>
#include <cxpr/context.h>
#include <cxpr/ir_view.h>
#include <cxpr/registry.h>
#include <cxpr/basket.h>
#include <cxpr/evaluator.h>
#include <cxpr/expression.h>
#include <cxpr/provider.h>
#include <cxpr/plugin.h>
#include <cxpr/plugins/c.h>
#include <cxpr/plugins/cuda.h>
#include <cxpr/plugins/graph.h>
#include <cxpr/plugins/meta.h>
#include <cxpr/runtime_call.h>
#include <cxpr/source_plan.h>
#include <cxpr/thread.h>
#include <cxpr/typecheck.h>

#endif /* CXPR_H */
