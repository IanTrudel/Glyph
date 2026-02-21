// Closure conversion pass.
// Lambdas capturing variables are converted to:
//   1. An environment struct containing captured values
//   2. A plain function taking the env struct as a hidden first parameter
// Non-capturing lambdas become plain function pointers.
//
// This will be implemented when we need closures in codegen.
